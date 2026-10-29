#include "Epub.h"

#include <FsHelpers.h>
#include <GifImageLayout.h>
#include <GifLimitedWriter.h>
#include <GifToBmpConverter.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <Txt.h>
#include <Utf8.h>
#include <ZipFile.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <xp_book.h>

#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#include "Epub/parsers/PackageParser.h"

Epub::Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(this->filepath));
}

bool Epub::findContentOpfFile(std::string* contentOpfFile, ZipFile* sharedZip) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  // Get file size without loading it all into heap
  const bool sizeFound = sharedZip ? sharedZip->getInflatedFileSize(containerPath, &containerSize)
                                   : getItemSize(containerPath, &containerSize);
  if (!sizeFound) {
    LOG_ERR("EBP", "Could not find or size META-INF/container.xml");
    return false;
  }

  PackageParser containerParser(XP_PKG_CONTAINER, "", containerSize, "");

  if (!containerParser.setup()) {
    return false;
  }

  // Stream read (reusing your existing stream logic)
  const bool read = sharedZip ? sharedZip->readFileToStream(containerPath, containerParser, 512)
                              : readItemContentsToStream(containerPath, containerParser, 512);
  if (!read) {
    LOG_ERR("EBP", "Could not read META-INF/container.xml");
    return false;
  }

  // Extract the result
  if (containerParser.view(0).empty()) {
    LOG_ERR("EBP", "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = containerParser.view();
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, const bool writeSpineEntries,
                           const bool metadataOnly, ZipFile* sharedZip) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath, sharedZip)) {
    LOG_ERR("EBP", "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_DBG("EBP", "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  const bool sizeFound = sharedZip ? sharedZip->getInflatedFileSize(contentOpfFilePath.c_str(), &contentOpfSize)
                                   : getItemSize(contentOpfFilePath, &contentOpfSize);
  if (!sizeFound) {
    LOG_ERR("EBP", "Could not get size of content.opf");
    return false;
  }

  PackageParser opfParser(XP_PKG_OPF_PASS, getBasePath(), contentOpfSize, getCachePath(),
                          writeSpineEntries ? bookMetadataCache.get() : nullptr, metadataOnly);
  if (!opfParser.setup()) {
    LOG_ERR("EBP", "Could not setup content.opf parser");
    return false;
  }

  const bool read = sharedZip ? sharedZip->readFileToStream(contentOpfFilePath.c_str(), opfParser, 1024, metadataOnly)
                              : readItemContentsToStream(contentOpfFilePath, opfParser, 1024, metadataOnly);
  if (!read) {
    LOG_ERR("EBP", "Could not read content.opf");
    return false;
  }

  // Grab data from opfParser into epub. Normalize titles to NFC so NFD (combining
  // mark) text renders correctly — the device fonts have no mark positioning.
  bookMetadata.title = utf8ComposeNfc(opfParser.value(XP_PKG_TITLE));
  bookMetadata.author = utf8ComposeNfc(opfParser.value(XP_PKG_AUTHOR));
  bookMetadata.language = opfParser.value(XP_PKG_LANGUAGE);

  if (metadataOnly) {
    LOG_DBG("EBP", "Successfully parsed package metadata");
    return true;
  }

  bookMetadata.coverItemHref = opfParser.value(XP_PKG_COVER);

  // Guide-based cover fallback: if no cover found via metadata/properties,
  // try extracting the image reference from the guide's cover page XHTML
  if (bookMetadata.coverItemHref.empty() && !opfParser.view(XP_PKG_GUIDE).empty()) {
    LOG_DBG("EBP", "No cover from metadata, trying guide cover page: %s", opfParser.value(XP_PKG_GUIDE).c_str());
    size_t coverPageSize;
    uint8_t* coverPageData = readItemContentsToBytes(opfParser.value(XP_PKG_GUIDE), &coverPageSize, true);
    if (coverPageData) {
      const std::string coverPageHtml(reinterpret_cast<char*>(coverPageData), coverPageSize);
      free(coverPageData);

      // Determine base path of the cover page for resolving relative image references
      std::string coverPageBase;
      const auto lastSlash = opfParser.value(XP_PKG_GUIDE).rfind('/');
      if (lastSlash != std::string::npos) {
        coverPageBase = opfParser.value(XP_PKG_GUIDE).substr(0, lastSlash + 1);
      }

      // Search for image references: xlink:href="..." (SVG) and src="..." (img)
      std::string imageRef;
      for (const char* pattern : {"xlink:href=\"", "src=\""}) {
        auto pos = coverPageHtml.find(pattern);
        while (pos != std::string::npos) {
          pos += strlen(pattern);
          const auto endPos = coverPageHtml.find('"', pos);
          if (endPos != std::string::npos) {
            const auto ref = std::string_view{coverPageHtml}.substr(pos, endPos - pos);
            // Cover BMP generation supports JPG/PNG/GIF. Skip unsupported wrappers so a later
            // supported raster image can still be selected.
            if (FsHelpers::hasPngExtension(ref) || FsHelpers::hasJpgExtension(ref) || FsHelpers::hasGifExtension(ref)) {
              imageRef = ref;
              break;
            }
          }
          pos = coverPageHtml.find(pattern, pos);
        }
        if (!imageRef.empty()) break;
      }

      if (!imageRef.empty()) {
        bookMetadata.coverItemHref = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(coverPageBase + imageRef));
        LOG_DBG("EBP", "Found cover image from guide: %s", bookMetadata.coverItemHref.c_str());
      }
    }
  }

  bookMetadata.textReferenceHref = opfParser.value(XP_PKG_TEXT);

  if (!opfParser.view(XP_PKG_NCX).empty()) {
    tocNcxItem = opfParser.value(XP_PKG_NCX);
  }

  if (!opfParser.view(XP_PKG_NAV).empty()) {
    tocNavItem = opfParser.value(XP_PKG_NAV);
  }

  auto parsedCss = opfParser.cssFiles();
  if (!parsedCss.empty()) cssFiles = std::move(parsedCss);

  LOG_DBG("EBP", "Successfully parsed content.opf");
  return true;
}

bool Epub::parseTocNcxFile() const {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    LOG_DBG("EBP", "No ncx file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc ncx file: %s", tocNcxItem.c_str());

  size_t ncxSize;
  if (!getItemSize(tocNcxItem, &ncxSize)) {
    LOG_ERR("EBP", "Could not get size of toc ncx file");
    return false;
  }

  PackageParser ncxParser(XP_PKG_NCX_PASS, contentBasePath, ncxSize, "", bookMetadataCache.get());

  if (!ncxParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc ncx parser");
    return false;
  }

  // Stream the decompressed NCX straight into the parser instead of round-tripping
  // through a temp file on the SD card (decompress -> write -> reopen -> reread -> delete).
  if (!readItemContentsToStream(tocNcxItem, ncxParser, 1024)) {
    LOG_ERR("EBP", "Could not read toc ncx file");
    return false;
  }

  LOG_DBG("EBP", "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile() const {
  // the nav file should have been specified in the content.opf file (EPUB 3)
  if (tocNavItem.empty()) {
    LOG_DBG("EBP", "No nav file specified");
    return false;
  }

  LOG_DBG("EBP", "Parsing toc nav file: %s", tocNavItem.c_str());

  size_t navSize;
  if (!getItemSize(tocNavItem, &navSize)) {
    LOG_ERR("EBP", "Could not get size of toc nav file");
    return false;
  }

  // Note: We can't use `contentBasePath` here as the nav file may be in a different folder to the content.opf
  // and the HTMLX nav file will have hrefs relative to itself
  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  PackageParser navParser(XP_PKG_NAV_PASS, navContentBasePath, navSize, "", bookMetadataCache.get());

  if (!navParser.setup()) {
    LOG_ERR("EBP", "Could not setup toc nav parser");
    return false;
  }

  // Stream the decompressed nav document straight into the parser instead of round-tripping
  // through a temp file on the SD card (decompress -> write -> reopen -> reread -> delete).
  if (!readItemContentsToStream(tocNavItem, navParser, 1024)) {
    LOG_ERR("EBP", "Could not read toc nav file");
    return false;
  }

  LOG_DBG("EBP", "Parsed TOC nav items");
  return true;
}

void Epub::discoverCssFilesFromZip() {
  const std::string& opfDir = contentBasePath;
  ZipFile zf(filepath);

  if (!zf.enumerateFilePaths([&](std::string_view filePath) {
        if (!opfDir.empty() && filePath.find(opfDir) != 0) {
          return;
        }

        if (!FsHelpers::hasCssExtension(filePath)) {
          return;
        }

        if (std::find(cssFiles.begin(), cssFiles.end(), filePath) != cssFiles.end()) {
          return;
        }

        LOG_DBG("EBP", "Discovered CSS file via ZIP enumeration: %.*s", (int)filePath.size(), filePath.data());
        cssFiles.push_back(std::string{filePath});
      })) {
    LOG_ERR("EBP", "Failed to enumerate ZIP file paths for CSS discovery");
  }
}

CssParser::ParseResult Epub::parseCssFiles(const CssParser::CacheStatus existingCacheStatus) const {
  // Maximum CSS file size we'll attempt to parse (uncompressed)
  // Larger files risk memory exhaustion on ESP32
  constexpr size_t MAX_CSS_FILE_SIZE = 128 * 1024;  // 128KB
  // Minimum heap required before attempting CSS parsing
  constexpr size_t MIN_HEAP_FOR_CSS_PARSING = 64 * 1024;  // 64KB

  if (cssFiles.empty()) {
    LOG_DBG("EBP", "No CSS files to parse, but CssParser created for inline styles");
  }

  LOG_DBG("EBP", "CSS files to parse: %zu", cssFiles.size());

  const bool hasPartialCache = existingCacheStatus == CssParser::CacheStatus::Partial;
  cssParser->clear();

  // Some converters emit one byte-identical stylesheet per chapter (100+ .css
  // entries), and each parse costs a zip locate plus an SD extract round-trip.
  // Match each normalized CSS path to its central-directory (CRC32,
  // compressed size) without throwing container allocations, then parse only
  // the first of each identical pair. If scratch allocation fails, parsing all
  // stylesheets is slower but remains correct.
  std::unique_ptr<XpBookCssEntry[]> dedupEntries;
  if (cssFiles.size() > 1) {
    dedupEntries = makeUniqueNoThrow<XpBookCssEntry[]>(cssFiles.size());
  }
  if (dedupEntries) {
    for (size_t i = 0; i < cssFiles.size(); i++) {
      dedupEntries[i].pathHash = ZipFile::fnvHash64(cssFiles[i].data(), cssFiles[i].size());
      dedupEntries[i].cssIndex = i;
    }
    std::sort(dedupEntries.get(), dedupEntries.get() + cssFiles.size(),
              [](const XpBookCssEntry& lhs, const XpBookCssEntry& rhs) { return lhs.pathHash < rhs.pathHash; });

    ZipFile(filepath).enumerateFileEntries([&](std::string_view entryPath, uint32_t crc32, uint32_t compressedSize) {
      xp_book_css_observe(dedupEntries.get(), cssFiles.size(),
                          {reinterpret_cast<const uint8_t*>(entryPath.data()), entryPath.size()}, crc32, compressedSize,
                          const_cast<std::vector<std::string>*>(&cssFiles),
                          [](void* opaque, size_t index) -> XpBookSpan {
                            const auto& path = (*static_cast<const std::vector<std::string>*>(opaque))[index];
                            return {reinterpret_cast<const uint8_t*>(path.data()), path.size()};
                          });
    });
    std::sort(dedupEntries.get(), dedupEntries.get() + cssFiles.size(),
              [](const XpBookCssEntry& lhs, const XpBookCssEntry& rhs) { return lhs.cssIndex < rhs.cssIndex; });
  } else if (cssFiles.size() > 1) {
    LOG_ERR("EBP", "Insufficient heap for CSS deduplication; parsing every stylesheet");
  }

  size_t skippedDuplicates = 0;
  CssParser::ParseResult parseResult = CssParser::ParseResult::Complete;

  // No cache yet - parse CSS files
  for (size_t cssIndex = 0; cssIndex < cssFiles.size(); cssIndex++) {
    const auto& cssPath = cssFiles[cssIndex];
    if (xp_book_css_duplicate(dedupEntries.get(), cssFiles.size(), cssIndex)) {
      skippedDuplicates++;
      continue;
    }
    LOG_DBG("EBP", "Parsing CSS file: %s", cssPath.c_str());

    // Check heap before parsing - CSS parsing allocates heavily
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_HEAP_FOR_CSS_PARSING) {
      LOG_ERR("EBP", "Insufficient heap for CSS parsing (%u bytes free, need %zu), skipping: %s", freeHeap,
              MIN_HEAP_FOR_CSS_PARSING, cssPath.c_str());
      if (parseResult == CssParser::ParseResult::Complete) {
        parseResult = CssParser::ParseResult::Partial;
      }
      continue;
    }

    // Check CSS file size before decompressing - skip files that are too large
    size_t cssFileSize = 0;
    if (getItemSize(cssPath, &cssFileSize)) {
      if (cssFileSize > MAX_CSS_FILE_SIZE) {
        LOG_ERR("EBP", "CSS file too large (%zu bytes > %zu max), skipping: %s", cssFileSize, MAX_CSS_FILE_SIZE,
                cssPath.c_str());
        if (parseResult == CssParser::ParseResult::Complete) {
          parseResult = CssParser::ParseResult::Partial;
        }
        continue;
      }
    }

    // Extract CSS file to temp location
    const auto tmpCssPath = getCachePath() + "/.tmp.css";
    HalFile tempCssFile;
    if (!Storage.openFileForWrite("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not create temp CSS file");
      parseResult = CssParser::ParseResult::Error;
      continue;
    }
    if (!readItemContentsToStream(cssPath, tempCssFile, 1024)) {
      LOG_ERR("EBP", "Could not read CSS file: %s", cssPath.c_str());
      // Explicitly close() file before calling Storage.remove()
      tempCssFile.close();
      Storage.remove(tmpCssPath.c_str());
      parseResult = CssParser::ParseResult::Error;
      continue;
    }
    // Explicitly close() file before reopening for reading
    tempCssFile.close();

    // Parse the CSS file
    if (!Storage.openFileForRead("EBP", tmpCssPath, tempCssFile)) {
      LOG_ERR("EBP", "Could not open temp CSS file for reading");
      Storage.remove(tmpCssPath.c_str());
      parseResult = CssParser::ParseResult::Error;
      continue;
    }
    const CssParser::ParseResult streamResult = cssParser->loadFromStream(tempCssFile);
    // Explicitly close() file before calling Storage.remove()
    tempCssFile.close();
    Storage.remove(tmpCssPath.c_str());
    if (streamResult == CssParser::ParseResult::Error) {
      parseResult = CssParser::ParseResult::Error;
    } else if (streamResult == CssParser::ParseResult::Partial && parseResult == CssParser::ParseResult::Complete) {
      parseResult = CssParser::ParseResult::Partial;
    }
  }

  if (parseResult == CssParser::ParseResult::Error) {
    LOG_ERR("EBP", "CSS parse failed; preserving any previous cache for a later retry");
    cssParser->clear();
    return parseResult;
  }

  if (parseResult == CssParser::ParseResult::Partial && cssParser->empty()) {
    LOG_ERR("EBP", "CSS parsing stopped before any usable rules were loaded; cache will not be replaced");
    cssParser->clear();
    return CssParser::ParseResult::Error;
  }

  if (parseResult == CssParser::ParseResult::Partial && hasPartialCache) {
    LOG_DBG("EBP", "CSS retry remained partial; preserving the previous partial cache");
    cssParser->clear();
    return parseResult;
  }

  // A partial cache remains useful for this session, but its header ensures a
  // later EPUB load retries the source stylesheets when more heap is available.
  if (!cssParser->saveToCache(parseResult == CssParser::ParseResult::Complete)) {
    LOG_ERR("EBP", "Failed to save CSS rules to cache");
    cssParser->clear();
    return CssParser::ParseResult::Error;
  }

  LOG_DBG("EBP", "Loaded %zu CSS style rules from %zu files (%zu identical duplicates skipped, %s)",
          cssParser->ruleCount(), cssFiles.size(), skippedDuplicates,
          parseResult == CssParser::ParseResult::Complete ? "complete" : "partial");
  cssParser->clear();
  return parseResult;
}

// load in the meta data for the epub file
// Opens the optional encrypted-entry accessor. A null result without an error
// means normal ZIP reads should be used. A hard error refuses the open with a
// user-presentable reason.
bool Epub::openProtection() {
  std::string err;
  decryptor = freeink::content::openProtectedBook(filepath, err);
  if (!err.empty()) {
    LOG_ERR("EBP", "protected content unavailable: %s", err.c_str());
    protectionError = err;
    return false;
  }
  if (decryptor) {
    LOG_DBG("EBP", "protected content; on-read access path open");
  }
  return true;
}

bool Epub::load(const bool buildIfMissing, const bool skipLoadingCss) {
  struct Context {
    Epub& epub;
    std::optional<BookMetadataCache::BookMetadata> metadata;
  } context{*this, std::nullopt};
  static_assert(static_cast<uint8_t>(CssParser::CacheStatus::Complete) == 1 &&
                static_cast<uint8_t>(CssParser::CacheStatus::Partial) == 2 &&
                static_cast<uint8_t>(CssParser::CacheStatus::Invalid) == 3);
  static_assert(static_cast<uint8_t>(CssParser::CacheLoadResult::Complete) == 0 &&
                static_cast<uint8_t>(CssParser::CacheLoadResult::LowMemory) == 1 &&
                static_cast<uint8_t>(CssParser::CacheLoadResult::Invalid) == 2);
  static_assert(static_cast<uint8_t>(CssParser::ParseResult::Complete) == 0 &&
                static_cast<uint8_t>(CssParser::ParseResult::Partial) == 1 &&
                static_cast<uint8_t>(CssParser::ParseResult::Error) == 2);
  const XpBookServices services{
      &context,
      [](void* opaque, uint32_t operation, uint32_t argument) -> uint32_t {
        auto& context = *static_cast<Context*>(opaque);
        auto& epub = context.epub;
        switch (static_cast<XpBookOperation>(operation)) {
          case XP_BOOK_OPEN_PROTECTION:
            return epub.openProtection();
          case XP_BOOK_NEW_METADATA:
            epub.bookMetadataCache = makeUniqueNoThrow<BookMetadataCache>(epub.cachePath);
            return bool(epub.bookMetadataCache);
          case XP_BOOK_NEW_CSS:
            epub.cssParser = makeUniqueNoThrow<CssParser>(epub.cachePath);
            return bool(epub.cssParser);
          case XP_BOOK_LOAD_METADATA:
            return epub.bookMetadataCache->load();
          case XP_BOOK_IS_TEXT:
            return Txt::isTxtOrMd(epub.filepath);
          case XP_BOOK_VALIDATE_TEXT:
            return Txt::validateCache(epub.filepath, epub.cachePath, epub.bookMetadataCache->getCumulativeSize(0));
          case XP_BOOK_BUILD_TEXT:
            return Txt::buildTxtCache(epub.filepath, epub.cachePath, epub.bookMetadataCache);
          case XP_BOOK_CSS_INSPECT:
            return static_cast<uint32_t>(epub.cssParser->inspectCache());
          case XP_BOOK_CSS_LOAD:
            return static_cast<uint32_t>(epub.cssParser->loadFromCache());
          case XP_BOOK_CSS_DELETE:
            epub.cssParser->deleteCache();
            break;
          case XP_BOOK_COPY_METADATA:
            context.metadata.emplace(epub.bookMetadataCache->coreMetadata);
            break;
          case XP_BOOK_PARSE_OPF:
            if (!context.metadata) context.metadata.emplace();
            return epub.parseContentOpf(*context.metadata, argument != 0);
          case XP_BOOK_DISCOVER_CSS:
            epub.discoverCssFilesFromZip();
            break;
          case XP_BOOK_RELEASE_METADATA:
            epub.bookMetadataCache.reset();
            break;
          case XP_BOOK_PARSE_CSS:
            return static_cast<uint32_t>(epub.parseCssFiles(static_cast<CssParser::CacheStatus>(argument)));
          case XP_BOOK_REMOVE_SECTIONS:
            Storage.removeDir((epub.cachePath + "/sections").c_str());
            break;
          case XP_BOOK_CSS_CLEAR:
            epub.cssParser->clear();
            break;
          case XP_BOOK_SETUP_CACHE:
            epub.setupCacheDir();
            break;
          case XP_BOOK_BEGIN_WRITE:
            return epub.bookMetadataCache->beginWrite();
          case XP_BOOK_BEGIN_OPF:
            return epub.bookMetadataCache->beginContentOpfPass();
          case XP_BOOK_END_OPF:
            return epub.bookMetadataCache->endContentOpfPass();
          case XP_BOOK_BEGIN_TOC:
            return epub.bookMetadataCache->beginTocPass();
          case XP_BOOK_HAS_NAV:
            return !epub.tocNavItem.empty();
          case XP_BOOK_PARSE_NAV:
            return epub.parseTocNavFile();
          case XP_BOOK_HAS_NCX:
            return !epub.tocNcxItem.empty();
          case XP_BOOK_PARSE_NCX:
            return epub.parseTocNcxFile();
          case XP_BOOK_END_TOC:
            return epub.bookMetadataCache->endTocPass();
          case XP_BOOK_END_WRITE:
            return epub.bookMetadataCache->endWrite();
          case XP_BOOK_BUILD_BOOK:
            return epub.bookMetadataCache->buildBookBin(epub.filepath, *context.metadata);
          case XP_BOOK_CLEAN_TMP:
            return epub.bookMetadataCache->cleanupTmpFiles();
          case XP_BOOK_RELEASE_WORK_METADATA:
            context.metadata.reset();
            break;
        }
        return 0;
      },
      [](void*) -> uint32_t { return millis(); },
      [](void* opaque, uint32_t event, uint32_t elapsed) {
        auto& epub = static_cast<Context*>(opaque)->epub;
        switch (static_cast<XpBookDiagnostic>(event)) {
          case XP_BOOK_LOG_LOADING:
            LOG_DBG("EBP", "Loading ePub: %s", epub.filepath.c_str());
            break;
          case XP_BOOK_LOG_OOM_CACHE_CSS:
            LOG_ERR("EBP", "OOM: metadata cache or CSS parser");
            break;
          case XP_BOOK_LOG_TEXT_INVALID:
            LOG_DBG("EBP", "TXT/MD cache invalid or outdated: %s", epub.filepath.c_str());
            break;
          case XP_BOOK_LOG_OOM_TEXT_CACHE:
            LOG_ERR("EBP", "OOM: TXT/MD metadata cache");
            break;
          case XP_BOOK_LOG_TEXT_LOADED:
            LOG_DBG("EBP", "Loaded TXT/MD from cache: %s", epub.filepath.c_str());
            break;
          case XP_BOOK_LOG_CSS_LOW_MEMORY:
            LOG_ERR("EBP", "Insufficient heap to load CSS cache; keeping it for a later retry");
            break;
          case XP_BOOK_LOG_CSS_REBUILD:
            LOG_DBG("EBP", "CSS cache missing, partial, or invalid; attempting to parse source stylesheets");
            break;
          case XP_BOOK_LOG_CSS_OPF_FAILED:
            LOG_ERR("EBP", "Could not parse content.opf from cached bookMetadata for CSS files");
            break;
          case XP_BOOK_LOG_CSS_RELOAD_FAILED:
            LOG_ERR("EBP", "Failed to reload cache after CSS rebuild");
            break;
          case XP_BOOK_LOG_LOADED:
            LOG_DBG("EBP", "Loaded ePub: %s", epub.filepath.c_str());
            break;
          case XP_BOOK_LOG_BUILDING:
            LOG_DBG("EBP", "Cache not found, building spine/TOC cache");
            break;
          case XP_BOOK_LOG_BEGIN_WRITE_FAILED:
            LOG_ERR("EBP", "Could not begin writing cache");
            break;
          case XP_BOOK_LOG_BEGIN_OPF_FAILED:
            LOG_ERR("EBP", "Could not begin writing content.opf pass");
            break;
          case XP_BOOK_LOG_OPF_FAILED:
            LOG_ERR("EBP", "Could not parse content.opf");
            break;
          case XP_BOOK_LOG_END_OPF_FAILED:
            LOG_ERR("EBP", "Could not end writing content.opf pass");
            break;
          case XP_BOOK_LOG_OPF_TIME:
            LOG_DBG("EBP", "OPF pass completed in %lu ms", static_cast<unsigned long>(elapsed));
            break;
          case XP_BOOK_LOG_BEGIN_TOC_FAILED:
            LOG_ERR("EBP", "Could not begin writing toc pass");
            break;
          case XP_BOOK_LOG_NAV_ATTEMPT:
            LOG_DBG("EBP", "Attempting to parse EPUB 3 nav document");
            break;
          case XP_BOOK_LOG_NCX_FALLBACK:
            LOG_DBG("EBP", "Falling back to NCX TOC");
            break;
          case XP_BOOK_LOG_TOC_FAILED:
            LOG_ERR("EBP", "Warning: Could not parse any TOC format");
            break;
          case XP_BOOK_LOG_END_TOC_FAILED:
            LOG_ERR("EBP", "Could not end writing toc pass");
            break;
          case XP_BOOK_LOG_TOC_TIME:
            LOG_DBG("EBP", "TOC pass completed in %lu ms", static_cast<unsigned long>(elapsed));
            break;
          case XP_BOOK_LOG_END_WRITE_FAILED:
            LOG_ERR("EBP", "Could not end writing cache");
            break;
          case XP_BOOK_LOG_BUILD_FAILED:
            LOG_ERR("EBP", "Could not update mappings and sizes");
            break;
          case XP_BOOK_LOG_BUILD_TIME:
            LOG_DBG("EBP", "buildBookBin completed in %lu ms", static_cast<unsigned long>(elapsed));
            break;
          case XP_BOOK_LOG_TOTAL_TIME:
            LOG_DBG("EBP", "Total indexing completed in %lu ms", static_cast<unsigned long>(elapsed));
            break;
          case XP_BOOK_LOG_CLEAN_TMP_FAILED:
            LOG_DBG("EBP", "Could not cleanup tmp files - ignoring");
            break;
          case XP_BOOK_LOG_RELOAD_FAILED:
            LOG_ERR("EBP", "Failed to reload cache after writing");
            break;
        }
      }};
  return xp_book_load(&services, buildIfMissing, skipLoadingCss) != 0;
}

bool Epub::loadMetadata(std::string& title, std::string& author) {
  struct Context {
    Epub& epub;
    std::string& title;
    std::string& author;
    std::unique_ptr<BookMetadataCache> cache;
  } context{*this, title, author, {}};
  const XpBookMetadataServices services{
      &context,
      [](void* opaque, uint32_t operation) -> uint8_t {
        auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<XpBookMetadataOperation>(operation)) {
          case XP_BOOK_META_CLEAR_OUTPUT:
            ctx.title.clear();
            ctx.author.clear();
            break;
          case XP_BOOK_META_IS_TEXT:
            return Txt::isTxtOrMd(ctx.epub.filepath);
          case XP_BOOK_META_TEXT_TITLE:
            ctx.title = utf8ComposeNfc(FsHelpers::getFileNameWithoutExtension(ctx.epub.filepath));
            break;
          case XP_BOOK_META_NEW_CACHE:
            ctx.cache = makeUniqueNoThrow<BookMetadataCache>(ctx.epub.cachePath);
            return bool(ctx.cache);
          case XP_BOOK_META_LOAD_CACHE:
            return ctx.cache->load();
          case XP_BOOK_META_COPY_CACHE:
            ctx.title = ctx.cache->coreMetadata.title;
            ctx.author = ctx.cache->coreMetadata.author;
            break;
          case XP_BOOK_META_DROP_CACHE:
            ctx.cache.reset();
            break;
          case XP_BOOK_META_LOAD_SOURCE:
            return ctx.epub.loadMetadataFromSource(ctx.title, ctx.author);
          default:
            return false;  // This adapter only supports metadata lookup operations.
        }
        return true;
      },
      [](void*, uint32_t) { LOG_ERR("EBP", "Could not allocate metadata cache reader"); }};
  return xp_book_metadata(&services) != 0;
}

bool Epub::loadMetadataFromSource(std::string& title, std::string& author) {
  struct Context {
    Epub& epub;
    std::string& title;
    std::string& author;
    ZipFile zip;
    BookMetadataCache::BookMetadata metadata;
  } context{*this, title, author, ZipFile(filepath), {}};
  static_assert(sizeof(void*) != 4 || sizeof(Context) <= 256);
  const XpBookMetadataServices services{&context,
                                        [](void* opaque, uint32_t operation) -> uint8_t {
                                          auto& ctx = *static_cast<Context*>(opaque);
                                          switch (static_cast<XpBookMetadataOperation>(operation)) {
                                            case XP_BOOK_META_OPEN_SOURCE:
                                              return ctx.zip.open();
                                            case XP_BOOK_META_PARSE_SOURCE:
                                              return ctx.epub.parseContentOpf(ctx.metadata, false, true, &ctx.zip);
                                            case XP_BOOK_META_CLOSE_SOURCE:
                                              ctx.zip.close();
                                              break;
                                            case XP_BOOK_META_COPY_SOURCE:
                                              ctx.title = std::move(ctx.metadata.title);
                                              ctx.author = std::move(ctx.metadata.author);
                                              break;
                                            default:
                                              return false;  // Source lookup has its own borrowed ZIP and metadata.
                                          }
                                          return true;
                                        },
                                        [](void* opaque, uint32_t) {
                                          const auto& ctx = *static_cast<Context*>(opaque);
                                          LOG_DBG("EBP", "Could not open ePub for package metadata: %s",
                                                  ctx.epub.filepath.c_str());
                                        }};
  return xp_book_metadata_source(&services) != 0;
}

bool Epub::loadSyncMetadata(SyncMetadata& metadata) {
  struct Context {
    Epub& epub;
    SyncMetadata& result;
    ZipFile zip;
    std::string path{};
    std::string base{};
    size_t size = 0;
    std::optional<PackageParser> parser{};
  } context{*this, metadata, ZipFile(filepath)};
  static_assert(sizeof(void*) != 4 || sizeof(Context) <= 256);
  const XpBookMetadataServices services{
      &context,
      [](void* opaque, uint32_t operation) -> uint8_t {
        auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<XpBookMetadataOperation>(operation)) {
          case XP_BOOK_META_CLEAR_OUTPUT:
            ctx.result = {};
            break;
          case XP_BOOK_META_OPEN_SOURCE:
            return ctx.zip.open();
          case XP_BOOK_META_FIND_OPF:
            return ctx.epub.findContentOpfFile(&ctx.path, &ctx.zip);
          case XP_BOOK_META_SIZE_OPF:
            return ctx.zip.getInflatedFileSize(ctx.path.c_str(), &ctx.size);
          case XP_BOOK_META_SETUP_OPF:
            ctx.base = ctx.path.substr(0, ctx.path.find_last_of('/') + 1);
            ctx.parser.emplace(XP_PKG_OPF_PASS, ctx.base, ctx.size, ctx.epub.cachePath, nullptr, true);
            return ctx.parser->setup();
          case XP_BOOK_META_READ_OPF:
            return ctx.zip.readFileToStream(ctx.path.c_str(), *ctx.parser, 1024, true);
          case XP_BOOK_META_CLOSE_SOURCE:
            ctx.zip.close();
            break;
          case XP_BOOK_META_COPY_SYNC:
            ctx.result.isbn = ctx.parser->value(XP_PKG_ISBN);
            ctx.result.asin = ctx.parser->value(XP_PKG_ASIN);
            ctx.result.series = ctx.parser->value(XP_PKG_SERIES);
            ctx.result.seriesIndex = ctx.parser->seriesIndex();
            break;
          default:
            return false;  // Sync lookup does not use the metadata cache.
        }
        return true;
      },
      [](void* opaque, uint32_t) {
        const auto& ctx = *static_cast<Context*>(opaque);
        LOG_DBG("EBP", "Could not open ePub for sync metadata: %s", ctx.epub.filepath.c_str());
      }};
  return xp_book_sync_metadata(&services) != 0;
}

XpBookMetadataServices Epub::cacheServices() const {
  return {const_cast<Epub*>(this),
          [](void* opaque, uint32_t operation) -> uint8_t {
            const auto& epub = *static_cast<const Epub*>(opaque);
            switch (static_cast<XpBookMetadataOperation>(operation)) {
              case XP_BOOK_META_CACHE_EXISTS:
                return Storage.exists(epub.cachePath.c_str());
              case XP_BOOK_META_CACHE_REMOVE:
                return Storage.removeDir(epub.cachePath.c_str());
              case XP_BOOK_META_CACHE_MKDIR:
                return Storage.mkdir(epub.cachePath.c_str());
              default:
                return false;  // Directory operations share no parser/cache state.
            }
          },
          [](void*, uint32_t event) {
            switch (static_cast<XpBookMetadataDiagnostic>(event)) {
              case XP_BOOK_META_LOG_CACHE_ABSENT:
                LOG_DBG("EPB", "Cache does not exist, no action needed");
                break;
              case XP_BOOK_META_LOG_CACHE_CLEAR_FAILED:
                LOG_ERR("EPB", "Failed to clear cache");
                break;
              case XP_BOOK_META_LOG_CACHE_CLEARED:
                LOG_DBG("EPB", "Cache cleared successfully");
                break;
              default:
                break;  // Only directory diagnostics belong to this adapter.
            }
          }};
}

bool Epub::clearCache() const {
  const auto services = cacheServices();
  return xp_book_clear_cache(&services) != 0;
}

void Epub::setupCacheDir() const {
  const auto services = cacheServices();
  xp_book_setup_cache(&services);
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return blank;
  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return blank;
  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return blank;
  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverBmpPath(bool cropped, bool originalThresholds) const {
  const auto coverFileName =
      std::string("cover") + (originalThresholds ? "_original" : "_legacy_v2") + (cropped ? "_crop" : "");
  return cachePath + "/" + coverFileName + ".bmp";
}

namespace {
StaticSemaphore_t gifGenerationMutexStorage;
const SemaphoreHandle_t gifGenerationMutex = xSemaphoreCreateMutexStatic(&gifGenerationMutexStorage);

bool extractGifCover(const Epub& epub, const std::string& href, const std::string& path) {
  HalFile file;
  struct Context {
    const Epub& epub;
    const std::string& href;
    const std::string& path;
    HalFile& file;
  } context{epub, href, path, file};
  const XpBookGifExtractServices services{
      &context,
      [](void* opaque, uint32_t operation) -> uint8_t {
        auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<XpBookGifExtractOperation>(operation)) {
          case XP_BOOK_GIF_OPEN:
            return Storage.openFileForWrite("EBP", ctx.path, ctx.file);
          case XP_BOOK_GIF_STREAM: {
            GifLimitedWriter limited(ctx.file, GifCommon::MAX_FILE_BYTES);
            return ctx.epub.readItemContentsToStream(ctx.href, limited, 1024);
          }
          case XP_BOOK_GIF_CLOSE:
            return ctx.file.close();
          case XP_BOOK_GIF_REMOVE:
            return Storage.remove(ctx.path.c_str());
          case XP_BOOK_GIF_EXISTS:
            return Storage.exists(ctx.path.c_str());
        }
        return 0;
      },
      [](void* opaque, uint32_t event) {
        auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<XpBookGifExtractDiagnostic>(event)) {
          case XP_BOOK_GIF_OPEN_FAILED:
            LOG_ERR("EBP", "Failed to create GIF extraction file: %s", ctx.path.c_str());
            break;
          case XP_BOOK_GIF_STREAM_FAILED:
            LOG_ERR("EBP", "GIF extraction failed, exceeded byte limit, or could not close: %s", ctx.path.c_str());
            break;
          case XP_BOOK_GIF_REMOVE_FAILED:
            LOG_ERR("EBP", "Failed to remove GIF temporary file: %s", ctx.path.c_str());
            break;
        }
      }};
  return xp_book_gif_extract(&services) != 0;
}
}  // namespace

enum class BookCoverMode : uint32_t { Cover, CachedThumbnail, SourceThumbnail };
enum class BookImageKind : uint32_t { Unsupported, Jpeg, Png, Gif };

struct Epub::CoverContext {
  const Epub& epub;
  BookCoverMode mode;
  int height;
  bool cropped;
  bool original;
  const std::string* href = nullptr;
  std::optional<std::string> ownedHref{};
  std::string sourcePath{};
  std::string gifTarget{};
  std::string gifOutput{};
  std::optional<HalFile> source{};
  std::optional<HalFile> output{};
  std::unique_ptr<BookMetadataCache::BookMetadata> metadata{};
  std::unique_ptr<ZipFile> zip{};

  std::string target() const {
    return mode == BookCoverMode::Cover ? epub.getCoverBmpPath(cropped, original) : epub.getThumbBmpPath(height);
  }
};

bool Epub::generateBmp(CoverContext& context) const {
  static_assert(sizeof(void*) != 4 || sizeof(CoverContext) <= 256);
  const XpBookCoverServices services{
      &context,
      [](void* opaque, uint32_t operation, uint32_t argument) -> uint8_t {
        auto& ctx = *static_cast<CoverContext*>(opaque);
        const auto& epub = ctx.epub;
        switch (static_cast<XpBookCoverOperation>(operation)) {
          case XP_BOOK_COVER_TARGET_EXISTS:
            if (ctx.gifTarget.empty()) return Storage.exists(ctx.target().c_str());
            return Storage.exists(ctx.gifTarget.c_str());
          case XP_BOOK_COVER_CACHE_LOADED:
            return epub.bookMetadataCache && epub.bookMetadataCache->isLoaded();
          case XP_BOOK_COVER_COPY_COVER:
            ctx.ownedHref.emplace(epub.bookMetadataCache->coreMetadata.coverItemHref);
            ctx.href = &*ctx.ownedHref;
            break;
          case XP_BOOK_COVER_BORROW_COVER:
            ctx.href = &epub.bookMetadataCache->coreMetadata.coverItemHref;
            break;
          case XP_BOOK_COVER_IS_TEXT:
            return Txt::isTxtOrMd(epub.filepath);
          case XP_BOOK_COVER_CONVERT_TEXT:
            return Txt::convertCoverImageToBmp(
                *ctx.href, ctx.target(), ctx.mode == BookCoverMode::Cover ? 0 : ctx.height, ctx.cropped, ctx.original);
          case XP_BOOK_COVER_COMPANION_COVER:
            ctx.ownedHref.emplace(Txt::findCompanionCoverImage(epub.filepath));
            ctx.href = &*ctx.ownedHref;
            break;
          case XP_BOOK_COVER_NEW_SOURCE:
            ctx.metadata = makeUniqueNoThrow<BookMetadataCache::BookMetadata>();
            ctx.zip = makeUniqueNoThrow<ZipFile>(epub.filepath);
            return ctx.metadata && ctx.zip;
          case XP_BOOK_COVER_SOURCE_ZIP_OPEN:
            return ctx.zip->open();
          case XP_BOOK_COVER_PARSE_SOURCE:
            return const_cast<Epub&>(epub).parseContentOpf(*ctx.metadata, false, false, ctx.zip.get());
          case XP_BOOK_COVER_RELEASE_SOURCE_ZIP:
            ctx.zip.reset();
            break;
          case XP_BOOK_COVER_HAS_PROTECTION:
            return bool(epub.decryptor);
          case XP_BOOK_COVER_OPEN_PROTECTION:
            return const_cast<Epub&>(epub).openProtection();
          case XP_BOOK_COVER_SETUP_CACHE:
            epub.setupCacheDir();
            break;
          case XP_BOOK_COVER_SOURCE_COVER:
            ctx.href = &ctx.metadata->coverItemHref;
            break;
          case XP_BOOK_COVER_MAKE_RASTER_SOURCE:
            ctx.sourcePath = epub.cachePath +
                             (argument == static_cast<uint32_t>(BookImageKind::Jpeg) ? "/.cover.jpg" : "/.cover.png");
            break;
          case XP_BOOK_COVER_EXTRACT_RASTER:
            return epub.extractItemToFile(*ctx.href, ctx.sourcePath);
          case XP_BOOK_COVER_OPEN_SOURCE:
            ctx.source.emplace();
            return Storage.openFileForRead("EBP", ctx.sourcePath, *ctx.source);
          case XP_BOOK_COVER_OPEN_TARGET:
            ctx.output.emplace();
            if (ctx.gifOutput.empty()) return Storage.openFileForWrite("EBP", ctx.target(), *ctx.output);
            return Storage.openFileForWrite("EBP", ctx.gifOutput, *ctx.output);
          case XP_BOOK_COVER_CLOSE_SOURCE:
            return ctx.source->close();
          case XP_BOOK_COVER_CLOSE_TARGET:
            return ctx.output->close();
          case XP_BOOK_COVER_REMOVE_SOURCE:
            return Storage.remove(ctx.sourcePath.c_str());
          case XP_BOOK_COVER_REMOVE_TARGET:
            return Storage.remove(ctx.target().c_str());
          case XP_BOOK_COVER_CREATE_EMPTY: {
            HalFile file;
            return Storage.openFileForWrite("EBP", ctx.target(), file);
          }
          case XP_BOOK_COVER_PREPARE_GIF_TARGET:
            ctx.gifTarget = ctx.target();
            break;
          case XP_BOOK_COVER_LOCK:
            return gifGenerationMutex && xSemaphoreTake(gifGenerationMutex, portMAX_DELAY) == pdTRUE;
          case XP_BOOK_COVER_UNLOCK:
            xSemaphoreGive(gifGenerationMutex);
            break;
          case XP_BOOK_COVER_MAKE_GIF_PATHS:
            ctx.sourcePath = epub.cachePath + "/.cover.gif";
            ctx.gifOutput = ctx.gifTarget + ".tmp";
            break;
          case XP_BOOK_COVER_EXTRACT_GIF:
            return extractGifCover(epub, *ctx.href, ctx.sourcePath);
          case XP_BOOK_COVER_RENAME_TARGET:
            return Storage.rename(ctx.gifOutput.c_str(), ctx.gifTarget.c_str());
          case XP_BOOK_COVER_EXISTS_SOURCE:
            return Storage.exists(ctx.sourcePath.c_str());
          case XP_BOOK_COVER_EXISTS_TARGET_TEMP:
            return Storage.exists(ctx.gifOutput.c_str());
          case XP_BOOK_COVER_REMOVE_TARGET_TEMP:
            return Storage.remove(ctx.gifOutput.c_str());
          case XP_BOOK_COVER_RELEASE_GIF_HANDLES:
            ctx.output.reset();
            ctx.source.reset();
            std::string{}.swap(ctx.gifOutput);
            std::string{}.swap(ctx.sourcePath);
            break;
        }
        return 0;
      },
      [](void* opaque) -> XpBookSpan {
        const auto* href = static_cast<CoverContext*>(opaque)->href;
        return href ? XpBookSpan{reinterpret_cast<const uint8_t*>(href->data()), href->size()} : XpBookSpan{};
      },
      [](void* opaque, uint32_t kind, uint8_t thumb, int32_t width, int32_t height) -> uint8_t {
        auto& ctx = *static_cast<CoverContext*>(opaque);
        switch (static_cast<BookImageKind>(kind)) {
          case BookImageKind::Jpeg:
            return thumb ? JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(*ctx.source, *ctx.output, width, height)
                         : JpegToBmpConverter::jpegFileToBmpStream(*ctx.source, *ctx.output, ctx.cropped, ctx.original);
          case BookImageKind::Png:
            return thumb ? PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(*ctx.source, *ctx.output, width, height)
                         : PngToBmpConverter::pngFileToBmpStream(*ctx.source, *ctx.output, ctx.cropped, ctx.original);
          case BookImageKind::Gif:
            return thumb ? GifToBmpConverter::gifFileTo1BitBmpStreamWithSize(*ctx.source, *ctx.output, width, height)
                         : GifToBmpConverter::gifFileToBmpStream(*ctx.source, *ctx.output, ctx.cropped, ctx.original);
          case BookImageKind::Unsupported:
            return false;
        }
        return false;
      },
      [](void* opaque, uint32_t event, uint32_t value) {
        auto& ctx = *static_cast<CoverContext*>(opaque);
        switch (static_cast<XpBookCoverDiagnostic>(event)) {
          case XP_BOOK_COVER_LOG_CACHE_COVER:
            LOG_ERR("EBP", "Cannot generate cover BMP, cache not loaded");
            break;
          case XP_BOOK_COVER_LOG_EMPTY_COVER:
            LOG_ERR("EBP", "No known cover image");
            break;
          case XP_BOOK_COVER_LOG_JPEG_COVER_START:
            LOG_DBG("EBP", "Generating BMP from JPG cover image (%s mode%s)", ctx.cropped ? "cropped" : "fit",
                    ctx.original ? ", original thresholds" : "");
            break;
          case XP_BOOK_COVER_LOG_PNG_COVER_START:
            LOG_DBG("EBP", "Generating BMP from PNG cover image (%s mode%s)", ctx.cropped ? "cropped" : "fit",
                    ctx.original ? ", original thresholds" : "");
            break;
          case XP_BOOK_COVER_LOG_PNG_COVER_FAILED:
            LOG_ERR("EBP", "Failed to generate BMP from PNG cover image");
            break;
          case XP_BOOK_COVER_LOG_COVER_CONVERT_FAILED:
            LOG_ERR("EBP", "Failed to generate BMP from cover image");
            break;
          case XP_BOOK_COVER_LOG_JPEG_COVER_END:
            LOG_DBG("EBP", "Generated BMP from JPG cover image, success: %s", value ? "yes" : "no");
            break;
          case XP_BOOK_COVER_LOG_PNG_COVER_END:
            LOG_DBG("EBP", "Generated BMP from PNG cover image, success: %s", value ? "yes" : "no");
            break;
          case XP_BOOK_COVER_LOG_UNSUPPORTED_COVER:
            LOG_ERR("EBP", "Cover image is not a supported format, skipping");
            break;
          case XP_BOOK_COVER_LOG_CACHE_THUMB:
            LOG_ERR("EBP", "Cannot generate thumb BMP, cache not loaded");
            break;
          case XP_BOOK_COVER_LOG_OOM_SOURCE:
            LOG_ERR("EBP", "OOM: cover metadata");
            break;
          case XP_BOOK_COVER_LOG_OPEN_SOURCE_METADATA:
            LOG_ERR("EBP", "Could not open EPUB for cover metadata");
            break;
          case XP_BOOK_COVER_LOG_EMPTY_THUMB:
            LOG_DBG("EBP", "No known cover image for thumbnail");
            break;
          case XP_BOOK_COVER_LOG_JPEG_THUMB_START:
            LOG_DBG("EBP", "Generating thumb BMP from JPG cover image");
            break;
          case XP_BOOK_COVER_LOG_PNG_THUMB_START:
            LOG_DBG("EBP", "Generating thumb BMP from PNG cover image");
            break;
          case XP_BOOK_COVER_LOG_JPEG_THUMB_FAILED:
            LOG_ERR("EBP", "Failed to generate thumb BMP from JPG cover image");
            break;
          case XP_BOOK_COVER_LOG_PNG_THUMB_FAILED:
            LOG_ERR("EBP", "Failed to generate thumb BMP from PNG cover image");
            break;
          case XP_BOOK_COVER_LOG_JPEG_THUMB_END:
            LOG_DBG("EBP", "Generated thumb BMP from JPG cover image, success: %s", value ? "yes" : "no");
            break;
          case XP_BOOK_COVER_LOG_PNG_THUMB_END:
            LOG_DBG("EBP", "Generated thumb BMP from PNG cover image, success: %s", value ? "yes" : "no");
            break;
          case XP_BOOK_COVER_LOG_UNSUPPORTED_THUMB:
            LOG_ERR("EBP", "Cover image is not a supported format, skipping thumbnail");
            break;
          case XP_BOOK_COVER_LOG_GIF_LOCK:
            LOG_ERR("EBP", "Failed to lock GIF cache generation");
            break;
          case XP_BOOK_COVER_LOG_GIF_OPEN_SOURCE:
            LOG_ERR("EBP", "Failed to open extracted GIF: %s", ctx.sourcePath.c_str());
            break;
          case XP_BOOK_COVER_LOG_GIF_OPEN_TARGET:
            LOG_ERR("EBP", "Failed to create GIF BMP: %s", ctx.gifTarget.c_str());
            break;
          case XP_BOOK_COVER_LOG_GIF_RENAME:
            LOG_ERR("EBP", "Failed to publish GIF BMP: %s", ctx.gifTarget.c_str());
            break;
          case XP_BOOK_COVER_LOG_GIF_REMOVE_TEMP:
            LOG_ERR("EBP", "Failed to remove GIF temporary file: %s", (value ? ctx.gifOutput : ctx.sourcePath).c_str());
            break;
        }
      }};
  return xp_book_cover(&services, static_cast<uint32_t>(context.mode), context.height) != 0;
}

bool Epub::generateCoverBmp(bool cropped, bool originalThresholds) const {
  CoverContext context{*this, BookCoverMode::Cover, 0, cropped, originalThresholds};
  return generateBmp(context);
}

std::string Epub::getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
std::string Epub::getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }

bool Epub::generateThumbBmp(int height) const {
  CoverContext context{*this, BookCoverMode::CachedThumbnail, height, false, false};
  return generateBmp(context);
}

bool Epub::generateThumbBmpFromSource(int height) {
  CoverContext context{*this, BookCoverMode::SourceThumbnail, height, false, false};
  return generateBmp(context);
}

struct Epub::ItemContext {
  const Epub& epub;
  const std::string& href;
  std::string path;
  Print* output = nullptr;
  const std::string* destination = nullptr;
  HalFile* file = nullptr;
};

XpBookIoServices Epub::itemServices(ItemContext& context) {
  return {&context,
          [](void* opaque, uint32_t operation) -> size_t {
            auto& ctx = *static_cast<ItemContext*>(opaque);
            switch (static_cast<XpBookIoControl>(operation)) {
              case XP_BOOK_IO_NORMALIZE:
                ctx.path = FsHelpers::normalisePath(ctx.href);
                break;
              case XP_BOOK_IO_IS_TEXT:
                return Txt::isTxtOrMd(ctx.epub.filepath);
              case XP_BOOK_IO_IS_ENCRYPTED:
                return ctx.epub.decryptor && ctx.epub.decryptor->isEncrypted(ctx.path);
              case XP_BOOK_IO_PLAIN_SIZE:
                return ctx.epub.decryptor->decryptedSize(ctx.path);
              case XP_BOOK_IO_OPEN_EXTRACT:
                return Storage.openFileForWrite("EBP", *ctx.destination, *ctx.file);
              case XP_BOOK_IO_FLUSH_EXTRACT:
                ctx.file->flush();
                break;
              case XP_BOOK_IO_CLOSE_EXTRACT:
                ctx.file->close();
                break;
              case XP_BOOK_IO_REMOVE_EXTRACT:
                Storage.remove(ctx.destination->c_str());
                break;
            }
            return 0;
          },
          [](void* opaque, size_t* size, uint8_t trailing) {
            auto& ctx = *static_cast<ItemContext*>(opaque);
            return ZipFile(ctx.epub.filepath).readFileToMemory(ctx.path.c_str(), size, trailing != 0);
          },
          [](void* opaque, uint32_t kind, size_t chunk, uint8_t early) -> uint8_t {
            auto& ctx = *static_cast<ItemContext*>(opaque);
            switch (static_cast<XpBookIoStream>(kind)) {
              case XP_BOOK_IO_TEXT:
                return Txt::streamTxtToHtml(ctx.epub.filepath, *ctx.output);
              case XP_BOOK_IO_ZIP:
                return ZipFile(ctx.epub.filepath).readFileToStream(ctx.path.c_str(), *ctx.output, chunk, early != 0);
              case XP_BOOK_IO_RESOURCE:
                return ctx.epub.readItemContentsToStream(ctx.href, *ctx.output, chunk, early != 0);
            }
            return 0;
          },
          [](void* opaque, XpBookSink append, void* sinkContext) -> uint8_t {
            auto& ctx = *static_cast<ItemContext*>(opaque);
            struct Sink {
              XpBookSink append;
              void* context;
            } sink{append, sinkContext};
            return ctx.epub.decryptor->decryptToSink(
                ctx.path,
                [](void* opaque, const uint8_t* data, size_t size) {
                  auto& sink = *static_cast<Sink*>(opaque);
                  return sink.append(sink.context, data, size) != 0;
                },
                &sink);
          },
          [](void* opaque, const uint8_t* data, size_t size) {
            return static_cast<ItemContext*>(opaque)->output->write(data, size);
          },
          [](void* opaque, uint8_t text, size_t* size) -> uint8_t {
            auto& ctx = *static_cast<ItemContext*>(opaque);
            if (!text) return ZipFile(ctx.epub.filepath).getInflatedFileSize(ctx.path.c_str(), size);
            HalFile file;
            if (!Storage.openFileForRead("EBP", ctx.epub.filepath, file)) return false;
            if (size) *size = file.size();
            return true;
          },
          [](size_t size) { return static_cast<uint8_t*>(malloc(size)); },
          [](uint8_t* bytes) { free(bytes); },
          [](void* opaque, uint32_t event, size_t total) {
            auto& ctx = *static_cast<ItemContext*>(opaque);
            switch (static_cast<XpBookIoDiagnostic>(event)) {
              case XP_BOOK_IO_EMPTY_HREF:
                LOG_DBG("EBP", "Failed to read item, empty href");
                break;
              case XP_BOOK_IO_MEMORY:
                LOG_ERR("EBP", "insufficient memory for %s (%u bytes)", ctx.path.c_str(), static_cast<unsigned>(total));
                break;
              case XP_BOOK_IO_READ_FAILED:
                LOG_ERR("EBP", "content read failed for %s", ctx.path.c_str());
                break;
              case XP_BOOK_IO_ZIP_READ_FAILED:
                LOG_DBG("EBP", "Failed to read item %s", ctx.path.c_str());
                break;
            }
          }};
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  ItemContext context{*this, itemHref, {}};
  const auto services = itemServices(context);
  return xp_book_read_bytes(&services, itemHref.empty(), size, trailingNullByte);
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    const bool allowEarlyStop) const {
  ItemContext context{*this, itemHref, {}, &out};
  const auto services = itemServices(context);
  return xp_book_read_stream(&services, itemHref.empty(), chunkSize, allowEarlyStop) != 0;
}

bool Epub::extractItemToFile(const std::string& itemHref, const std::string& destPath) const {
  HalFile out;
  ItemContext context{*this, itemHref, {}, &out, &destPath, &out};
  const auto services = itemServices(context);
  return xp_book_extract(&services) != 0;
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  ItemContext context{*this, itemHref, {}};
  const auto services = itemServices(context);
  return xp_book_item_size(&services, size) != 0;
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

size_t Epub::getCumulativeSpineItemSize(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getCumulativeSize(spineIndex);
}

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR("EBP", "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_DBG("EBP", "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_DBG("EBP", "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR("EBP", "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_DBG("EBP", "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

int Epub::getSpineIndexForSize(const size_t bytes) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || static_cast<uint64_t>(bytes) > UINT32_MAX) return -1;
  return bookMetadataCache->getSpineIndexForSize(static_cast<uint32_t>(bytes));
}

size_t Epub::getBookSize() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || bookMetadataCache->getSpineCount() == 0) {
    return 0;
  }
  return getCumulativeSpineItemSize(getSpineItemsCount() - 1);
}

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR("EBP", "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG("EBP", "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    // there was no textReference in epub, so we return 0 (the first chapter)
    return 0;
  }

  // loop through spine items to get the correct index matching the text href
  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG("EBP", "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(),
              i);
      return i;
    }
  }
  // This should not happen, as we checked for empty textReferenceHref earlier
  LOG_DBG("EBP", "Section not found for text reference");
  return 0;
}

// Calculate progress in book (returns 0.0-1.0)
float Epub::calculateProgress(const int currentSpineIndex, const float currentSpineRead) const {
  const size_t bookSize = getBookSize();
  if (bookSize == 0) {
    return 0.0f;
  }
  const size_t prevChapterSize = (currentSpineIndex >= 1) ? getCumulativeSpineItemSize(currentSpineIndex - 1) : 0;
  return xp_book_progress(bookSize, prevChapterSize, getCumulativeSpineItemSize(currentSpineIndex), currentSpineRead);
}

int Epub::resolveHrefToSpineIndex(const std::string& href) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return -1;

  // Split before decoding so escaped '#' characters in filenames stay part of the path.
  const size_t hashPos = href.find('#');
  const std::string rawTarget = hashPos != std::string::npos ? href.substr(0, hashPos) : href;
  const std::string target = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(rawTarget));

  // Same-file reference (anchor-only)
  if (target.empty()) return -1;

  // Extract just the filename for comparison
  size_t targetSlash = target.find_last_of('/');
  std::string targetFilename = (targetSlash != std::string::npos) ? target.substr(targetSlash + 1) : target;

  for (int i = 0; i < getSpineItemsCount(); i++) {
    const auto& spineHref = getSpineItem(i).href;
    // Try exact match first
    if (spineHref == target) return i;
    // Then filename-only match
    size_t spineSlash = spineHref.find_last_of('/');
    std::string spineFilename = (spineSlash != std::string::npos) ? spineHref.substr(spineSlash + 1) : spineHref;
    if (spineFilename == targetFilename) return i;
  }
  return -1;
}
