#include "Txt.h"

#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <JpegToBmpConverter.h>
#include <Logging.h>
#include <Memory.h>
#include <PngToBmpConverter.h>
#include <Serialization.h>
#include <Utf8.h>
#include <xp_ffi.h>

#include <cstring>
#include <optional>

#include "TxtToHtml.h"

bool Txt::isTxtOrMd(std::string_view path) {
  return FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path);
}

std::string Txt::findCompanionCoverImage(const std::string& filepath) {
  std::string folder = FsHelpers::extractFolderPath(filepath);

  std::string baseName = FsHelpers::getFileNameWithoutExtension(filepath);
  struct Context {
    const std::string& folder;
    const std::string& baseName;
    std::string found;
  } context{folder, baseName, {}};
  xp_book_txt_find_cover(&context, [](void* opaque, const uint8_t* name, const uint8_t* extension) {
    auto& ctx = *static_cast<Context*>(opaque);
    std::string path = name ? ctx.folder + "/" + std::string(reinterpret_cast<const char*>(name)) +
                                  reinterpret_cast<const char*>(extension)
                            : ctx.folder + "/" + ctx.baseName + reinterpret_cast<const char*>(extension);
    if (!Storage.exists(path.c_str())) return false;
    ctx.found = std::move(path);
    return true;
  });
  return std::move(context.found);
}

bool Txt::convertCoverImageToBmp(const std::string& imagePath, const std::string& destBmpPath, int thumbHeight,
                                 bool cropped, bool originalThresholds) {
  enum class Action : uint32_t { Exists, Open, Jpeg, Png, JpegThumb, PngThumb, Close, Remove };
  struct Context {
    const std::string& source;
    const std::string& destination;
    bool cropped, thresholds;
    HalFile src, dst;
  } context{imagePath, destBmpPath, cropped, originalThresholds, {}, {}};
  const XpBookTxtFileServices services{
      &context,
      [](void* opaque, uint32_t action, int32_t width, int32_t height) {
        auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<Action>(action)) {
          case Action::Exists:
            return Storage.exists(ctx.source.c_str());
          case Action::Open:
            return Storage.openFileForRead("TXT", ctx.source, ctx.src) &&
                   Storage.openFileForWrite("TXT", ctx.destination, ctx.dst);
          case Action::Jpeg:
            return JpegToBmpConverter::jpegFileToBmpStream(ctx.src, ctx.dst, ctx.cropped, ctx.thresholds);
          case Action::Png:
            return PngToBmpConverter::pngFileToBmpStream(ctx.src, ctx.dst, ctx.cropped, ctx.thresholds);
          case Action::JpegThumb:
            return JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(ctx.src, ctx.dst, width, height);
          case Action::PngThumb:
            return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(ctx.src, ctx.dst, width, height);
          case Action::Close:
            ctx.src.close();
            ctx.dst.close();
            return true;
          case Action::Remove:
            Storage.remove(ctx.destination.c_str());
            return true;
        }
        return false;
      },
      [](void* opaque, uint8_t* bytes, size_t size) -> int32_t {
        return static_cast<Context*>(opaque)->src.read(bytes, size);
      },
      [](void* opaque, const uint8_t* bytes, size_t size) {
        return static_cast<Context*>(opaque)->dst.write(bytes, size);
      },
      nullptr,
      nullptr};
  const uint32_t kind = FsHelpers::hasBmpExtension(imagePath)   ? 1
                        : FsHelpers::hasJpgExtension(imagePath) ? 2
                        : FsHelpers::hasPngExtension(imagePath) ? 3
                                                                : 0;
  return xp_book_txt_convert(&services, kind, thumbHeight);
}

bool Txt::streamTxtToHtml(const std::string& filepath, Print& out) {
  const uint32_t t0 = millis();
  HalFile src;
  if (!Storage.openFileForRead("TXT", filepath, src)) {
    LOG_ERR("TXT", "Failed to open TXT/MD for streaming: %s", filepath.c_str());
    return false;
  }

  const size_t srcSize = src.size();
  LOG_DBG("TXT", "Converting TXT/MD to HTML: %s (%zu bytes)", filepath.c_str(), srcSize);

  const bool ok = TxtToHtml::stream(
      filepath, &src, [](void* ctx, uint8_t* buf, size_t size) { return static_cast<HalFile*>(ctx)->read(buf, size); },
      out);

  LOG_DBG("TXT", "Converted TXT/MD to HTML in %lu ms (%zu bytes in)", millis() - t0, srcSize);
  return ok;
}

void Txt::invalidateCache(const std::string& cachePath) {
  Storage.removeDir((cachePath + "/html").c_str());
  Storage.removeDir((cachePath + "/sections").c_str());
}

bool Txt::validateCache(const std::string& filepath, const std::string& cachePath, size_t cachedSize) {
  enum class Action : uint32_t { Exists, OpenHtml, CloseHtml, OpenRaw, CloseRaw, Invalidate };
  const std::string htmlPath = cachePath + "/html/0.html";
  struct Context {
    const std::string& filepath;
    const std::string& cachePath;
    const std::string& htmlPath;
    size_t cachedSize;
    std::optional<HalFile> file;
  } context{filepath, cachePath, htmlPath, cachedSize, {}};
  const XpBookTxtFileServices services{
      &context,
      [](void* opaque, uint32_t operation, int32_t, int32_t) {
        auto& ctx = *static_cast<Context*>(opaque);
        switch (static_cast<Action>(operation)) {
          case Action::Exists:
            return Storage.exists(ctx.htmlPath.c_str());
          case Action::OpenHtml:
            return Storage.openFileForRead("TXT", ctx.htmlPath, ctx.file.emplace());
          case Action::OpenRaw:
            return Storage.openFileForRead("TXT", ctx.filepath, ctx.file.emplace());
          case Action::CloseHtml:
          case Action::CloseRaw:
            ctx.file.reset();
            return true;
          case Action::Invalidate:
            Txt::invalidateCache(ctx.cachePath);
            return true;
        }
        return false;
      },
      [](void* opaque, uint8_t* bytes, size_t size) -> int32_t {
        return static_cast<Context*>(opaque)->file->read(bytes, size);
      },
      nullptr,
      [](void* opaque) { return static_cast<Context*>(opaque)->file->size(); },
      [](void* opaque, uint32_t event, [[maybe_unused]] size_t size) {
        [[maybe_unused]] const auto& ctx = *static_cast<Context*>(opaque);
        switch (event) {
          case 0:
            LOG_DBG("TXT", "HTML cache version mismatch or missing, invalidating: %s", ctx.htmlPath.c_str());
            break;
          case 1:
            LOG_DBG("TXT", "File size changed (%u vs cached %u), invalidating cache", static_cast<uint32_t>(size),
                    static_cast<uint32_t>(ctx.cachedSize));
            break;
          case 2:
            LOG_DBG("TXT", "Cache invalid for %s, wiping html and sections", ctx.filepath.c_str());
            break;
        }
      }};
  const char* tag = TxtToHtml::cacheVersionTag(filepath);
  return xp_book_txt_validate(&services, cachedSize, reinterpret_cast<const uint8_t*>(tag), strlen(tag));
}

bool Txt::buildTxtCache(const std::string& filepath, const std::string& cachePath,
                        std::unique_ptr<BookMetadataCache>& bookMetadataCache) {
  LOG_DBG("TXT", "Building metadata cache for TXT: %s", filepath.c_str());

  enum class Action : uint32_t {
    Exists,
    Mkdir,
    Invalidate,
    Begin,
    BeginOpf,
    Spine,
    EndOpf,
    BeginToc,
    Toc,
    EndToc,
    End,
    Book,
    Cleanup,
    Reload,
    Complete = UINT32_MAX
  };
  struct Context {
    const std::string& filepath;
    const std::string& cachePath;
    std::unique_ptr<BookMetadataCache>& cache;
    std::string title;
    std::optional<BookMetadataCache::BookMetadata> metadata;
    std::string companionCover;
  } context{filepath, cachePath, bookMetadataCache, {}, {}, {}};
  const auto result = static_cast<Action>(xp_book_txt_build(&context, [](void* opaque, uint32_t operation) {
    auto& ctx = *static_cast<Context*>(opaque);
    switch (static_cast<Action>(operation)) {
      case Action::Exists:
        return Storage.exists(ctx.cachePath.c_str());
      case Action::Mkdir:
        Storage.mkdir(ctx.cachePath.c_str());
        return true;
      case Action::Invalidate:
        Txt::invalidateCache(ctx.cachePath);
        return true;
      case Action::Begin:
        return ctx.cache->beginWrite();
      case Action::BeginOpf:
        return ctx.cache->beginContentOpfPass();
      case Action::Spine:
        ctx.cache->createSpineEntry("content.html");
        return true;
      case Action::EndOpf:
        return ctx.cache->endContentOpfPass();
      case Action::BeginToc:
        return ctx.cache->beginTocPass();
      case Action::Toc:
        ctx.title = FsHelpers::getFileNameWithoutExtension(ctx.filepath);
        ctx.cache->createTocEntry(ctx.title, "content.html", "", 0);
        return true;
      case Action::EndToc:
        return ctx.cache->endTocPass();
      case Action::End:
        return ctx.cache->endWrite();
      case Action::Book: {
        auto& metadata = ctx.metadata.emplace();
        metadata.title = utf8ComposeNfc(ctx.title);
        metadata.language = "en";
        ctx.companionCover = Txt::findCompanionCoverImage(ctx.filepath);
        if (!ctx.companionCover.empty()) metadata.coverItemHref = ctx.companionCover;
        return ctx.cache->buildBookBin(ctx.filepath, metadata);
      }
      case Action::Cleanup:
        ctx.cache->cleanupTmpFiles();
        return true;
      case Action::Reload:
        ctx.cache = makeUniqueNoThrow<BookMetadataCache>(ctx.cachePath);
        return ctx.cache && ctx.cache->load();
      case Action::Complete:
        return true;
    }
    return false;
  }));
  switch (result) {
    case Action::Begin:
      LOG_ERR("TXT", "Could not begin writing cache");
      break;
    case Action::BeginOpf:
      LOG_ERR("TXT", "Could not begin writing content.opf pass");
      break;
    case Action::EndOpf:
      LOG_ERR("TXT", "Could not end writing content.opf pass");
      break;
    case Action::BeginToc:
      LOG_ERR("TXT", "Could not begin writing toc pass");
      break;
    case Action::EndToc:
      LOG_ERR("TXT", "Could not end writing toc pass");
      break;
    case Action::End:
      LOG_ERR("TXT", "Could not end writing cache");
      break;
    case Action::Book:
      LOG_ERR("TXT", "Could not build book.bin for TXT");
      break;
    case Action::Reload:
      LOG_ERR("TXT", "Failed to reload cache after build");
      break;
    case Action::Exists:
    case Action::Mkdir:
    case Action::Invalidate:
    case Action::Spine:
    case Action::Toc:
    case Action::Cleanup:
      break;
    case Action::Complete:
      return true;
  }
  return false;
}
