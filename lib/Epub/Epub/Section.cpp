#include "Section.h"

#include <BufferedFile.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <xp_book.h>

#include <algorithm>
#include <type_traits>

#include "Epub/css/CssParser.h"
#include "Page.h"
#include "SectionCodec.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: Support <ruby> and <rt> tags. Skip <rp> tags
// v34: Word gaps are only suppressed for tokens glued in the source, so spaces between
//      Hangul words survive again; ruby element boundaries carry the continuation flag
//      instead. Invalidates v33 caches, whose word positions have the spaces collapsed.

// v34: <br> handling changed layout — a <br> after text is now a margin-stripped
//      line break (browser-like) and only a <br> whose block stays empty injects
//      the scene-break gap, so cached pages laid out by older versions no longer
//      match. Keeps <br>-per-paragraph books (common CJK formatting) from
//      re-adding container spacing at every paragraph.
// v35: Persist a uint32_t visible-text start offset for every page.
// v36: Ruby and CJK justification layout changes invalidate cached word positions.
// v37: Footnote href records grew from 96 to 256 bytes.
// v38: Focus Reading line breaking changed — a visible hyphen/dash inside a word is now a
//      break opportunity, and hyphenation of a focus-split word considers the whole word
//      instead of only its regular-weight suffix. Pages cached by older versions were laid
//      out with the previous, more restrictive break set and no longer match.
// v39: Image top margin is clamped so a full-viewport-height image cannot
//      overflow the page bottom; older caches can hold placements that panels
//      with no bottom inset refuse to draw.
// v40: Ruby groups remain intact when a large text block is soft-flushed.
// v41: Simple HTML table rows are laid out as positioned columns instead of
//      flattened paragraphs with synthetic row/cell labels.
// v42: Closing a block strips inherited vertical margins and padding.
// v43: Paragraph base direction excludes direction changes from inline elements.
// v44: Persist internal-link rectangles with each page for touch navigation.
// v45: Internal EPUB links preserve CSS superscript/subscript positioning.
// v46: Ordered lists number their items, list-style-type: none suppresses markers,
//      and <ul>/<ol> containers contribute their own margins/padding to child insets.
// v47: Word and character spacing in the header (cache validation); cached BlockStyle stores only character spacing.
// v48: Hangul words wrap at spaces; with hyphenation on they may also split at a line end.
//      Justification no longer stretches between syllables.
// v49 was used by pre-release builds with a different header layout.
// v50: Paragraph indentation width in the header for cache validation.
// v51: Preserve paragraph continuity and top spacing across soft flushes.
// v52: Missing full-block and black-square symbols now have visible widths.
// v53: Persist per-word source ranges alongside the redaction layout changes.
// v54: Source ranges retain codepoints absorbed by NFC composition.
// v55: Persist the first logical word of each paragraph for clipping separators.
// v56: Rendering layout (fonts per word, drop caps, border boxes).
// v57: Includes explicit hyphen/dash line breaks and invalidates both v56 layouts.
// v58: Rebuild sections for corrected rendering layout and footnote placement.
// v59: Preserve stored CSS styles during low-heap layout.
// v60: Text running into a block or heading start stays in its own paragraph.
// Earlier GIF/table development builds also used versions 60 through 62.
// v63: Rebuild for corrected paragraph boundaries and supported static GIF images.
// v64: Explicit font inheritance preserves parent emphasis and changes text layout.
// v65: Count literal spaces after hyphens and ellipses when justifying text.
// v66: Omit carried no-break spaces at the start of wrapped lines.
// v67: Count only stretchable literal spaces after visual text reordering.
// v68: Leave text below drop caps unsplit until it is laid out at full width.
constexpr uint8_t SECTION_FILE_VERSION = XP_SEC_VERSION;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the visible-offset LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// The Rust codec derives the partial version from the final version.
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = XP_SEC_PARTIAL_VERSION;
}  // namespace

namespace {
struct SectionFileContext {
  HalFile& file;
  const std::string& path;
  std::string* text = nullptr;
  static size_t operation(void* raw, uint32_t operation, size_t argument) {
    auto& self = *static_cast<SectionFileContext*>(raw);
    switch (static_cast<XpBookFileOperation>(operation)) {
      case XP_BOOK_FILE_EXISTS_FINAL:
        return Storage.exists(self.path.c_str());
      case XP_BOOK_FILE_REMOVE_FINAL:
        return Storage.remove(self.path.c_str());
      case XP_BOOK_FILE_REMOVE_TEMPORARY:
        return Storage.remove((self.path + ".part").c_str());
      case XP_BOOK_FILE_EXISTS_TEMPORARY:
        return Storage.exists((self.path + ".part").c_str());
      case XP_BOOK_FILE_IS_OPEN:
        return bool(self.file);
      case XP_BOOK_FILE_PROMOTE_TEMPORARY:
        return 0;
      case XP_BOOK_FILE_OPEN_READ:
        return Storage.openFileForRead("SCT", self.path, self.file);
      case XP_BOOK_FILE_SEEK:
        return self.file.seek(argument);
      case XP_BOOK_FILE_SIZE:
        return self.file.size();
      case XP_BOOK_FILE_POSITION:
        return self.file.position();
      case XP_BOOK_FILE_CLOSE:
        return self.file.close();
      case XP_BOOK_FILE_RELEASE_TEXT:
        if (self.text) std::string{}.swap(*self.text);
        return 0;
    }
    return 0;
  }
  static size_t read(void* raw, uint8_t* bytes, size_t length) {
    return static_cast<SectionFileContext*>(raw)->file.read(bytes, length);
  }
  static void diagnostic(void*, uint32_t event, uint32_t a, uint32_t b) {
    switch (static_cast<XpBookSectionDiagnostic>(event)) {
      case XP_BOOK_SECTION_TRUNCATED_HEADER:
        LOG_ERR("SCT", "Deserialization failed: truncated section header");
        break;
      case XP_BOOK_SECTION_UNKNOWN_VERSION:
        LOG_ERR("SCT", "Deserialization failed: Unknown version %u", a);
        break;
      case XP_BOOK_SECTION_PARAMETERS:
        LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
        break;
      case XP_BOOK_SECTION_MALFORMED_PARTIAL:
        LOG_ERR("SCT", "Deserialization failed: malformed partial section");
        break;
      case XP_BOOK_SECTION_LOADED:
        LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", a, b ? " (partial)" : "");
        break;
      case XP_BOOK_SECTION_CACHE_MISSING:
        LOG_DBG("SCT", "Cache does not exist, no action needed");
        break;
      case XP_BOOK_SECTION_CACHE_CLEAR_FAILED:
        LOG_ERR("SCT", "Failed to clear cache");
        break;
      case XP_BOOK_SECTION_CACHE_CLEARED:
        LOG_DBG("SCT", "Cache cleared successfully");
        break;
      case XP_BOOK_SECTION_PAGE_FILE_CLOSED:
        LOG_ERR("SCT", "File not open for writing page %d", a);
        break;
      case XP_BOOK_SECTION_PAGE_WRITE_FAILED:
        LOG_ERR("SCT", "Failed to serialize page %d", a);
        break;
      case XP_BOOK_SECTION_PAGE_PROCESSED:
        LOG_DBG("SCT", "Page %d processed", a);
        break;
      case XP_BOOK_SECTION_HEADER_FILE_CLOSED:
        LOG_DBG("SCT", "File not open for writing header");
        break;

      case XP_BOOK_SECTION_WRITE_RECORD:
        LOG_ERR("SCT", "Failed to encode/write cache record %u", a);
        break;
      case XP_BOOK_SECTION_INVALID_LUT:
        LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
        break;
      case XP_BOOK_SECTION_PROMOTE:
        LOG_ERR("SCT", "Failed to move built section into place");
        break;
      case XP_BOOK_SECTION_SHORT_RECORD:
        LOG_ERR("SCT", "Short cache record %u", a);
        break;
      case XP_BOOK_SECTION_INVALID_RECORD:
        LOG_ERR("SCT", "Invalid cache record %u", a);
        break;
      case XP_BOOK_SECTION_INVALID_STRING:
        LOG_ERR("SCT", "Invalid cache string length %u", a);
        break;
      case XP_BOOK_SECTION_VISIBLE_SEEK:
        LOG_ERR("SCT", "Failed to seek visible-text LUT");
        break;
      case XP_BOOK_SECTION_VISIBLE_READ:
        LOG_ERR("SCT", "Failed to read visible-text LUT batch");
        break;
    }
  }
  static uint8_t* resize(void* raw, size_t length) {
    auto* text = static_cast<SectionFileContext*>(raw)->text;
    if (!text) return nullptr;
    text->resize(length);
    return reinterpret_cast<uint8_t*>(text->data());
  }
  static size_t write(void* raw, const uint8_t* bytes, size_t length) {
    return static_cast<SectionFileContext*>(raw)->file.write(bytes, length);
  }
  XpBookFileServices services() { return {this, operation, read, write, resize, diagnostic}; }
};
std::optional<uint32_t> sectionQuery(const std::string& path, const XpBookSectionQuery query, const uint32_t index = 0,
                                     const bool first = false) {
  HalFile file;
  SectionFileContext context{file, path};
  const auto services = context.services();
  uint32_t output[3];
  if (!xp_book_section_query(&services, query, index, first, output)) return std::nullopt;
  return output[0];
}
}  // namespace

struct Section::PolicyContext {
  Section& section;
  bool (*yieldNow)() = nullptr;
  static uint32_t operation(void* raw, const uint32_t operation, const uint32_t argument) {
    auto& context = *static_cast<PolicyContext*>(raw);
    auto& self = context.section;
    switch (static_cast<XpBookSectionOperation>(operation)) {
      case XP_BOOK_SECTION_OP_HAS_BUILD:
        return bool(self.build_);
      case XP_BOOK_SECTION_OP_HAS_PARSER:
        return self.build_ && self.build_->parser;
      case XP_BOOK_SECTION_OP_BUILT_PAGES:
        return self.builtPageCount_;
      case XP_BOOK_SECTION_OP_PARTIAL:
        return self.partial_;
      case XP_BOOK_SECTION_OP_PARTIAL_PAGES:
        return self.partialPageCount_;
      case XP_BOOK_SECTION_OP_TOTAL:
        return self.build_->totalBytes;
      case XP_BOOK_SECTION_OP_REUSED_HTML:
        return self.build_->reusedHtml;
      case XP_BOOK_SECTION_OP_PARSER_STEP:
        return static_cast<uint32_t>(self.build_->parser->parseStep());
      case XP_BOOK_SECTION_OP_PARSER_FINISH:
        return self.build_->parser->finishParse();
      case XP_BOOK_SECTION_OP_PARSER_ABORT:
        self.build_->parser->abortParse();
        break;
      case XP_BOOK_SECTION_OP_PARSER_CONSUMED:
        return self.build_->parser->parseBytesConsumed();
      case XP_BOOK_SECTION_OP_YIELD:
        return context.yieldNow && context.yieldNow();
      case XP_BOOK_SECTION_OP_CSS_CLEAR:
        if (self.build_->cssParser) self.build_->cssParser->clear();
        break;
      case XP_BOOK_SECTION_OP_RESET_BUILD:
        self.build_.reset();
        break;
      case XP_BOOK_SECTION_OP_FILE_OPEN:
        return bool(self.file);
      case XP_BOOK_SECTION_OP_FILE_CLOSE:
        self.file.close();
        break;
      case XP_BOOK_SECTION_OP_TMP_BIN_REMOVE:
        return Storage.remove(self.binTmpPath().c_str());
      case XP_BOOK_SECTION_OP_FINAL_EXISTS:
        return Storage.exists(self.filePath.c_str());
      case XP_BOOK_SECTION_OP_FINAL_REMOVE:
        return Storage.remove(self.filePath.c_str());
      case XP_BOOK_SECTION_OP_TMP_HTML_EXISTS:
        return Storage.exists(self.build_->tmpHtmlPath.c_str());
      case XP_BOOK_SECTION_OP_TMP_HTML_REMOVE:
        return Storage.remove(self.build_->tmpHtmlPath.c_str());
      case XP_BOOK_SECTION_OP_PROMOTE_HTML:
        return Storage.rename(self.build_->tmpHtmlPath.c_str(), self.build_->htmlPath.c_str());
      case XP_BOOK_SECTION_OP_COMMIT_FINAL:
        return self.commitBuildFile(SECTION_FILE_VERSION, 0, 0);
      case XP_BOOK_SECTION_OP_COMMIT_PARTIAL:
        return self.commitBuildFile(SECTION_FILE_PARTIAL_VERSION, argument, self.build_->totalBytes);
      case XP_BOOK_SECTION_OP_SET_COMPLETE:
        self.buildComplete_ = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_PARTIAL:
        self.partial_ = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_PARTIAL_PAGES:
        self.partialPageCount_ = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_PAGES:
        self.pageCount = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_BUILT_PAGES:
        self.builtPageCount_ = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_PARTIAL_CONSUMED:
        self.partialBytesConsumed_ = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_PARTIAL_TOTAL:
        self.partialTotalBytes_ = argument;
        break;
      case XP_BOOK_SECTION_OP_SET_CONSUMED:
        self.build_->bytesConsumed = argument;
        break;
    }
    return 0;
  }
  static void diagnostic(void*, uint32_t event, uint32_t argument) {
    switch (static_cast<XpBookSectionEvent>(event)) {
      case XP_BOOK_SECTION_EVENT_NO_BUILD:
        LOG_ERR("SCT", "buildSomeMore with no active build");
        break;
      case XP_BOOK_SECTION_EVENT_PARSE_ERROR:
        LOG_ERR("SCT", "Parse error during incremental build");
        break;
      case XP_BOOK_SECTION_EVENT_FINALIZE_ERROR:
        LOG_ERR("SCT", "Parse finalize failed; abandoning section build");
        break;
      case XP_BOOK_SECTION_EVENT_HTML_PROMOTION:
        LOG_DBG("SCT", "Failed to promote HTML cache, removing temp");
        break;
      case XP_BOOK_SECTION_EVENT_SUSPENDED:
        LOG_INF("SCT", "Suspended build: %u pages persisted", argument);
        break;
    }
  }
  XpBookServices services() { return {this, operation, nullptr, diagnostic}; }
};

struct Section::CommitContext {
  Section& section;
  static size_t operation(void* raw, uint32_t operation, size_t argument) {
    auto& self = static_cast<CommitContext*>(raw)->section;
    switch (static_cast<XpBookFileOperation>(operation)) {
      case XP_BOOK_FILE_SEEK:
        return self.file.seek(argument);
      case XP_BOOK_FILE_SIZE:
        return self.file.size();
      case XP_BOOK_FILE_POSITION:
        return self.file.position();
      case XP_BOOK_FILE_CLOSE:
        return self.file.close();
      case XP_BOOK_FILE_EXISTS_FINAL:
        return Storage.exists(self.filePath.c_str());
      case XP_BOOK_FILE_REMOVE_FINAL:
        return Storage.remove(self.filePath.c_str());
      case XP_BOOK_FILE_REMOVE_TEMPORARY:
        return Storage.remove(self.binTmpPath().c_str());
      case XP_BOOK_FILE_PROMOTE_TEMPORARY:
        return Storage.rename(self.binTmpPath().c_str(), self.filePath.c_str());
      case XP_BOOK_FILE_IS_OPEN:
        return bool(self.file);
      case XP_BOOK_FILE_EXISTS_TEMPORARY:
        return Storage.exists(self.binTmpPath().c_str());
      case XP_BOOK_FILE_OPEN_READ:
      case XP_BOOK_FILE_RELEASE_TEXT:
        return 0;
    }
    return 0;
  }
  static size_t write(void* raw, const uint8_t* bytes, size_t length) {
    return static_cast<CommitContext*>(raw)->section.file.write(bytes, length);
  }
  static uint32_t entry(void* raw, size_t index, uint32_t field) {
    const auto& entry = static_cast<CommitContext*>(raw)->section.build_->lut[index];
    switch (static_cast<XpBookLutField>(field)) {
      case XP_BOOK_LUT_FILE:
        return entry.fileOffset;
      case XP_BOOK_LUT_PARAGRAPH:
        return entry.paragraphIndex;
      case XP_BOOK_LUT_LIST:
        return entry.listItemIndex;
      case XP_BOOK_LUT_VISIBLE:
        return entry.visibleTextOffset;
    }
    return 0;
  }
  static void anchor(void* raw, size_t index, XpBookAnchor* out) {
    const auto& anchor = static_cast<CommitContext*>(raw)->section.build_->parser->getAnchors()[index];
    *out = {reinterpret_cast<const uint8_t*>(anchor.first.data()), anchor.first.size(), anchor.second};
  }
};

struct Section::StartContext {
  Section& section;
  const ReaderRenderSpec& spec;
  const std::function<void()>& popupFn;
  std::string localPath, htmlDir, htmlPath, tmpHtmlPath;
  std::optional<HalFile> tmpHtml;
  std::unique_ptr<BuildContext> build;
  std::vector<std::string> tocAnchors;
  std::optional<std::decay_t<decltype(section.epub->getTocItem(0))>> tocItem;
  StartContext(Section& section, const ReaderRenderSpec& spec, const std::function<void()>& popupFn)
      : section(section), spec(spec), popupFn(popupFn) {}
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<StartContext*>(raw);
    auto& self = c.section;
    switch (static_cast<XpBookSectionStartOperation>(operation)) {
      case XP_BOOK_START_HAS_BUILD:
        return bool(self.build_);
      case XP_BOOK_START_RELEASE_FONTS:
        if (auto* cache = self.renderer.getFontCacheManager()) cache->releaseSdFontCaches();
        break;
      case XP_BOOK_START_SET_COMPLETE:
        self.buildComplete_ = argument;
        break;
      case XP_BOOK_START_SET_BUILT_PAGES:
        self.builtPageCount_ = argument;
        break;
      case XP_BOOK_START_PARTIAL:
        return self.partial_;
      case XP_BOOK_START_PARTIAL_PAGES:
        return self.partialPageCount_;
      case XP_BOOK_START_SET_PAGES:
        self.pageCount = argument;
        break;
      case XP_BOOK_START_TMP_BIN_EXISTS:
        return Storage.exists(self.binTmpPath().c_str());
      case XP_BOOK_START_TMP_BIN_REMOVE:
        return Storage.remove(self.binTmpPath().c_str());
      case XP_BOOK_START_PREPARE_PATHS:
        c.localPath = self.epub->getSpineItem(self.spineIndex).href;
        c.htmlDir = self.epub->getCachePath() + "/html";
        c.htmlPath = c.htmlDir + "/" + std::to_string(self.spineIndex) + ".html";
        c.tmpHtmlPath = c.htmlDir + "/.tmp_" + std::to_string(self.spineIndex) + ".html";
        break;
      case XP_BOOK_START_MKDIR_SECTIONS:
        return Storage.mkdir((self.epub->getCachePath() + "/sections").c_str());
      case XP_BOOK_START_HTML_EXISTS:
        return Storage.exists(c.htmlPath.c_str());
      case XP_BOOK_START_MKDIR_HTML:
        return Storage.mkdir(c.htmlDir.c_str());
      case XP_BOOK_START_DELAY:
        delay(argument);
        break;
      case XP_BOOK_START_TMP_HTML_EXISTS:
        return Storage.exists(c.tmpHtmlPath.c_str());
      case XP_BOOK_START_TMP_HTML_REMOVE:
        return Storage.remove(c.tmpHtmlPath.c_str());
      case XP_BOOK_START_TMP_HTML_OPEN:
        c.tmpHtml.emplace();
        return Storage.openFileForWrite("SCT", c.tmpHtmlPath, *c.tmpHtml);
      case XP_BOOK_START_STREAM:
        return self.epub->readItemContentsToStream(c.localPath, *c.tmpHtml, argument);
      case XP_BOOK_START_TMP_HTML_SIZE:
        return c.tmpHtml->size();
      case XP_BOOK_START_TMP_HTML_CLOSE:
        return c.tmpHtml->close();
      case XP_BOOK_START_TMP_HTML_DROP:
        c.tmpHtml.reset();
        break;
      case XP_BOOK_START_PROMOTE_HTML:
        return Storage.rename(c.tmpHtmlPath.c_str(), c.htmlPath.c_str());
      case XP_BOOK_START_BIN_OPEN:
        return Storage.openFileForWrite("SCT", self.binTmpPath(), self.file);
      case XP_BOOK_START_WRITE_HEADER:
        self.writeSectionFileHeader(c.spec);
        break;
      case XP_BOOK_START_ALLOCATE_CONTEXT:
        c.build = makeUniqueNoThrow<BuildContext>();
        return bool(c.build);
      case XP_BOOK_START_CONFIGURE_CONTEXT:
        c.build->reusedHtml = argument;
        c.build->htmlPath = c.htmlPath;
        c.build->tmpHtmlPath = c.tmpHtmlPath;
        c.build->parsePath = argument ? c.htmlPath : c.tmpHtmlPath;
        {
          const size_t lastSlash = c.localPath.find_last_of('/');
          c.build->contentBase = lastSlash != std::string::npos ? c.localPath.substr(0, lastSlash + 1) : "";
        }
        c.build->imageBasePath = self.epub->getCachePath() + "/img_" + std::to_string(self.spineIndex) + "_";
        break;
      case XP_BOOK_START_EMBEDDED_STYLE:
        return c.spec.embeddedStyle;
      case XP_BOOK_START_GET_CSS:
        c.build->cssParser = self.epub->getCssParser();
        return c.build->cssParser != nullptr;
      case XP_BOOK_START_LOAD_CSS:
        return static_cast<uint32_t>(c.build->cssParser->loadFromCache());
      case XP_BOOK_START_CLEAR_CSS:
        if (c.build->cssParser) c.build->cssParser->clear();
        break;
      case XP_BOOK_START_FILE_CLOSE:
        return self.file.close();
      case XP_BOOK_START_CONTEXT_REUSED:
        return c.build->reusedHtml;
      case XP_BOOK_START_TOC_FIRST:
        return self.epub->getTocIndexForSpineIndex(self.spineIndex);
      case XP_BOOK_START_TOC_COUNT:
        return self.epub->getTocItemsCount();
      case XP_BOOK_START_TOC_READ:
        c.tocItem.emplace(self.epub->getTocItem(argument));
        break;
      case XP_BOOK_START_TOC_SPINE:
        return c.tocItem->spineIndex;
      case XP_BOOK_START_SPINE:
        return self.spineIndex;
      case XP_BOOK_START_TOC_LENGTH:
        return c.tocItem->anchor.size();
      case XP_BOOK_START_TOC_MOVE:
        c.tocAnchors.push_back(std::move(c.tocItem->anchor));
        break;
      case XP_BOOK_START_TOC_DROP:
        c.tocItem.reset();
        break;
      case XP_BOOK_START_ALLOCATE_PARSER:
        c.build->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
            self.epub, c.build->parsePath, self.renderer, c.spec.fontId, c.spec.lineCompression,
            c.spec.extraParagraphSpacing, c.spec.paragraphAlignment, c.spec.viewportWidth, c.spec.viewportHeight,
            c.spec.hyphenationEnabled, c.spec.focusReadingEnabled, nullptr, c.spec.embeddedStyle, c.build->contentBase,
            c.build->imageBasePath, c.spec.imageRendering, std::move(c.tocAnchors), c.popupFn, c.build->cssParser);
        return bool(c.build->parser);
      case XP_BOOK_START_CONFIGURE_PARSER: {
        auto* ctx = c.build.get();
        auto* section = &self;
        ctx->parser->setPageBytesCallback(
            [section, ctx](XpChapterSpan bytes, uint16_t paragraph, uint16_t list, uint32_t visible) {
              ctx->lut.push_back({section->onPageComplete(bytes.data, bytes.len), paragraph, list, visible});
            });
        ctx->parser->setTextSpacing(c.spec.characterSpacing, c.spec.wordSpacingPercent);
        ctx->parser->setParagraphIndentSpaces(c.spec.paragraphIndentSpaces);
        Hyphenator::setPreferredLanguage(self.epub->getLanguage());
        break;
      }
      case XP_BOOK_START_ACTIVATE:
        self.build_ = std::move(c.build);
        break;
      case XP_BOOK_START_BEGIN_PARSE:
        return self.build_->parser->beginParse();
      case XP_BOOK_START_ABANDON:
        self.abandonBuild();
        break;
      case XP_BOOK_START_SET_TOTAL:
        self.build_->totalBytes = self.build_->parser->parseTotalBytes();
        break;
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t argument) {
    auto& c = *static_cast<StartContext*>(raw);
    switch (static_cast<XpBookSectionStartEvent>(event)) {
      case XP_BOOK_START_EVENT_ACTIVE:
        LOG_ERR("SCT", "startBuild called while a build is already active");
        break;
      case XP_BOOK_START_EVENT_REUSED:
        LOG_DBG("SCT", "Reusing cached HTML %s", c.htmlPath.c_str());
        break;
      case XP_BOOK_START_EVENT_RETRY:
        LOG_DBG("SCT", "Retrying stream (attempt %d)...", argument);
        break;
      case XP_BOOK_START_EVENT_REMOVED_INCOMPLETE:
        LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
        break;
      case XP_BOOK_START_EVENT_STREAM_FAILED:
        LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
        break;
      case XP_BOOK_START_EVENT_STREAMED:
        LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", c.tmpHtmlPath.c_str(), argument);
        break;
      case XP_BOOK_START_EVENT_PROMOTION:
        LOG_DBG("SCT", "Failed to promote HTML cache; parsing from temp");
        break;
      case XP_BOOK_START_EVENT_CONTEXT_OOM:
        LOG_ERR("SCT", "OOM: BuildContext");
        break;
      case XP_BOOK_START_EVENT_CSS_LOW:
        LOG_ERR("SCT", "Insufficient heap to hydrate CSS; section build deferred");
        break;
      case XP_BOOK_START_EVENT_CSS_INVALID:
        LOG_ERR("SCT", "Failed to load CSS from cache");
        break;
      case XP_BOOK_START_EVENT_PARSER_OOM:
        LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser");
        break;
      case XP_BOOK_START_EVENT_BEGIN_FAILED:
        LOG_ERR("SCT", "Failed to begin parse");
        break;
    }
  }
};

struct Section::TocContext {
  const Section& section;
  std::unique_ptr<XpBookTocTarget[]> targets;
  HalFile file;
  std::optional<serialization::BufferedFileReader> reader;
  uint32_t start = 0;
  uint16_t count = 0;
  std::string cachedAnchor;
  const std::string* active = nullptr;
  std::optional<std::decay_t<decltype(section.epub->getTocItem(0))>> item;
  explicit TocContext(const Section& section) : section(section) {}
  static size_t operation(void* raw, uint32_t operation, size_t argument) {
    auto& c = *static_cast<TocContext*>(raw);
    const auto& self = c.section;
    switch (static_cast<XpBookTocOperation>(operation)) {
      case XP_BOOK_TOC_FIRST: {
        return self.epub->getTocIndexForSpineIndex(self.spineIndex);
      }
      case XP_BOOK_TOC_COUNT: {
        return self.epub->getTocItemsCount();
      }
      case XP_BOOK_TOC_ALLOCATE: {
        c.targets = makeUniqueNoThrow<XpBookTocTarget[]>(argument);
        if (!c.targets) LOG_ERR("SCT", "OOM: TOC anchor lookup");
        return reinterpret_cast<size_t>(c.targets.get());
      }
      case XP_BOOK_TOC_OPEN_ANCHORS: {
        if (self.openAnchorMap(c.file, c.count)) {
          c.start = c.file.position();
          c.reader.emplace(c.file, 1024);
        }
        return c.count;
      }
      case XP_BOOK_TOC_BUILD_ANCHORS: {
        return self.build_ && self.build_->parser ? self.build_->parser->getAnchors().size() : 0;
      }
      case XP_BOOK_TOC_SELECT_BUILD: {
        const auto& entry = self.build_->parser->getAnchors()[argument];
        c.active = &entry.first;
        return entry.second;
      }
      case XP_BOOK_TOC_SELECT_DISK: {
        uint16_t page;
        if (!sectioncodec::readAnchor(*c.reader, c.file.size(), c.cachedAnchor, page)) return SIZE_MAX;
        c.active = &c.cachedAnchor;
        return page;
      }
      case XP_BOOK_TOC_ACTIVE_HASH: {
        return std::hash<std::string>{}(*c.active);
      }
      case XP_BOOK_TOC_TOC_MATCHES: {
        return self.epub->getTocItem(argument).anchor == *c.active;
      }
      case XP_BOOK_TOC_BUILD_OVERRIDE: {
        return self.findAnchorDuringBuild(*c.active).value_or(argument);
      }
      case XP_BOOK_TOC_REWIND_DISK: {
        return c.reader && c.reader->seek(c.start);
      }
      case XP_BOOK_TOC_READ_TOC: {
        c.item.emplace(self.epub->getTocItem(argument));
        break;
      }
      case XP_BOOK_TOC_TOC_SPINE: {
        return c.item->spineIndex;
      }
      case XP_BOOK_TOC_SPINE: {
        return self.spineIndex;
      }
      case XP_BOOK_TOC_TOC_LENGTH: {
        return c.item->anchor.size();
      }
      case XP_BOOK_TOC_TOC_HASH: {
        return std::hash<std::string>{}(c.item->anchor);
      }
      case XP_BOOK_TOC_DROP_TOC: {
        c.item.reset();
        break;
      }
    }
    return 0;
  }
};

XpBookBuildRecords Section::buildRecords() const {
  return {const_cast<Section*>(this), build_ ? build_->lut.size() : 0,
          build_ && build_->parser ? build_->parser->getAnchors().size() : 0,
          [](void* raw, size_t index, uint32_t field) -> uint32_t {
            CommitContext context{*static_cast<Section*>(raw)};
            return CommitContext::entry(&context, index, field);
          },
          [](void* raw, size_t index, XpBookAnchor* out) {
            CommitContext context{*static_cast<Section*>(raw)};
            CommitContext::anchor(&context, index, out);
          }};
}

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() { suspendBuild(); }

uint32_t Section::onPageComplete(const uint8_t* bytes, const size_t length) {
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  return xp_book_section_append(&io, bytes, length, &builtPageCount_, &pageCount);
}

void Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  const auto header = sectioncodec::header(spec, pageCount);
  xp_book_section_header(&io, header.values);
}

bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  const auto expected = sectioncodec::header(spec, 0);
  XpBookSectionLoaded state{partialBytesConsumed_, partialTotalBytes_, pageCount, partialPageCount_, partial_};
  const bool ok = xp_book_section_load(&io, expected.values, &state);
  partialBytesConsumed_ = state.consumed;
  partialTotalBytes_ = state.total;
  pageCount = state.pages;
  partialPageCount_ = state.partial_pages;
  partial_ = state.partial;
  return ok;
}

bool Section::clearCache() const {
  HalFile unused;
  SectionFileContext context{unused, filePath};
  const auto io = context.services();
  return xp_book_section_clear(&io);
}

bool Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  // One-shot build: start, then lay out the whole section in a single pass.
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  if (!buildSomeMore(0)) {  // 0 = build to completion
    return false;
  }
  return buildComplete_;
}

bool Section::startBuild(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  StartContext context{*this, spec, popupFn};
  const XpBookServices services{&context, StartContext::operation, nullptr, StartContext::diagnostic};
  return xp_book_section_start(&services);
}

bool Section::buildSomeMore(const int maxPages, bool (*yieldNow)()) {
  PolicyContext context{*this, yieldNow};
  const auto services = context.services();
  return xp_book_section_transition(&services, XP_BOOK_SECTION_ADVANCE, maxPages);
}

bool Section::hasHtmlCache() const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  return Storage.exists(htmlPath.c_str());
}

std::optional<uint16_t> Section::findAnchorDuringBuild(const std::string& anchor) const {
  const auto records = buildRecords();
  uint32_t page;
  if (!xp_book_section_build_anchor(&records, reinterpret_cast<const uint8_t*>(anchor.data()), anchor.size(), &page))
    return std::nullopt;
  return page;
}

std::optional<uint16_t> Section::findAnchor(const std::string& anchor) const {
  HalFile file;
  std::string key;
  SectionFileContext context{file, filePath, &key};
  const auto io = context.services();
  const auto records = buildRecords();
  uint32_t page;
  if (!xp_book_section_find_anchor(&io, &records, reinterpret_cast<const uint8_t*>(anchor.data()), anchor.size(),
                                   &page))
    return std::nullopt;
  return page;
}

int Section::getTocIndexForPage(const int page) const {
  TocContext context{*this};
  const XpBookTocServices io{&context, TocContext::operation};
  return xp_book_section_toc(&io, page);
}

uint16_t Section::estimatedTotalPages() const {
  XpBookSectionEstimate state{build_ ? build_->bytesConsumed : 0,
                              build_ ? build_->totalBytes : 0,
                              partialBytesConsumed_,
                              partialTotalBytes_,
                              build_ ? build_->smoothedAtConsumed : 0,
                              build_ ? build_->smoothedEstimate : 0,
                              pageCount,
                              builtPageCount_,
                              partialPageCount_,
                              partial_,
                              bool(build_)};
  const auto result = xp_book_section_estimate(&state);
  if (build_) {
    build_->smoothedEstimate = state.smoothed;
    build_->smoothedAtConsumed = state.smoothed_at;
  }
  return result;
}

// Write the LUTs and anchor map into the open tmp .bin, patch the header with the built
// page count and table offsets, stamp `version` as the commit point, then swap the tmp
// file over filePath. For SECTION_FILE_PARTIAL_VERSION a watermark trailer
// (bytesConsumed, totalBytes) is appended after the visible-offset LUT so a later open can estimate
// the total page count. The parser must still be alive (anchors are read from it).
// On failure the tmp is removed and any pre-existing file at filePath is left intact.
bool Section::commitBuildFile(const uint8_t version, const uint32_t bytesConsumed, const uint32_t totalBytes) {
  CommitContext context{*this};
  const XpBookFileServices io{&context, CommitContext::operation,      nullptr, CommitContext::write,
                              nullptr,  SectionFileContext::diagnostic};
  const XpBookBuildRecords records{&context, build_->lut.size(), build_->parser->getAnchors().size(),
                                   CommitContext::entry, CommitContext::anchor};
  return xp_book_section_commit(&io, &records, version, builtPageCount_, bytesConsumed, totalBytes);
}

bool Section::finalizeBuild() {
  PolicyContext context{*this};
  const auto services = context.services();
  return xp_book_section_transition(&services, XP_BOOK_SECTION_FINISH, 0);
}

void Section::suspendBuild() {
  PolicyContext context{*this};
  const auto services = context.services();
  xp_book_section_transition(&services, XP_BOOK_SECTION_SUSPEND, 0);
}

void Section::abandonBuild() {
  PolicyContext context{*this};
  const auto services = context.services();
  xp_book_section_transition(&services, XP_BOOK_SECTION_ABANDON, 0);
}

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  const auto records = buildRecords();
  const XpBookPageLoader loader{&file, [](void* raw, uint32_t bytes, uint32_t visible) -> void* {
                                  return Page::load(*static_cast<HalFile*>(raw), bytes, visible).release();
                                }};
  return std::unique_ptr<Page>(static_cast<Page*>(xp_book_section_built_page(&io, &records, page, &loader)));
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const {
  HalFile file;
  SectionFileContext context{file, filePath};
  const auto services = context.services();
  uint32_t range[3];
  if (!xp_book_section_query(&services, XP_BOOK_SECTION_PAGE_RANGE, page, false, range)) return nullptr;
  auto result = Page::load(file, range[1] - range[0], range[2]);
  if (result) result->visibleTextOffset = range[2];
  return result;
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  switch (xp_book_section_page_source(page, build_ ? build_->lut.size() : 0, pageCount, partialPageCount_, bool(build_),
                                      partial_)) {
    case 1:
      return loadPageDuringBuild(page);
    case 2:
      return loadPageAt(page);
    default:
      return nullptr;
  }
}

std::string Section::getTextFromSectionFile() {
  std::string text;
  auto page = loadPage(currentPage);
  if (!page) return text;
  struct Context {
    Page& page;
    std::string& text;
  } context{*page, text};
  const XpBookPageText source{&context, static_cast<uint16_t>(page->elementCount()),
                              [](void* raw, uint16_t index, uint32_t field) -> uint32_t {
                                const auto line = static_cast<Context*>(raw)->page.element(index);
                                switch (field) {
                                  case 0:
                                    return line.getTag();
                                  case 1:
                                    return line.textBlock().valid();
                                  case 2:
                                    return line.textBlock().wordCount();
                                  default:
                                    return 0;
                                }
                              },
                              [](void* raw, uint16_t element, uint16_t index) -> XpBookTextSpan {
                                const char* word =
                                    static_cast<Context*>(raw)->page.element(element).textBlock().wordText(index);
                                return {reinterpret_cast<const uint8_t*>(word), strlen(word)};
                              },
                              [](void* raw, size_t length) -> uint8_t* {
                                auto& text = static_cast<Context*>(raw)->text;
                                text.resize(length);
                                return reinterpret_cast<uint8_t*>(text.data());
                              }};
  if (!xp_book_section_text(&source)) return {};
  return text;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  return sectionQuery(filePath, XP_BOOK_SECTION_PAGE_COUNT);
}

bool Section::openAnchorMap(HalFile& f, uint16_t& count) const {
  SectionFileContext context{f, filePath};
  const auto services = context.services();
  uint32_t output[3];
  if (!xp_book_section_query(&services, XP_BOOK_SECTION_ANCHOR_MAP, 0, false, output)) return false;
  count = output[0];
  return true;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile file;
  std::string key;
  SectionFileContext context{file, filePath, &key};
  const auto services = context.services();
  uint32_t page;
  if (!xp_book_section_anchor(&services, reinterpret_cast<const uint8_t*>(anchor.data()), anchor.size(), &page))
    return std::nullopt;
  return static_cast<uint16_t>(page);
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  return sectionQuery(filePath, XP_BOOK_SECTION_PARAGRAPH_PAGE, pIndex);
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  return sectionQuery(filePath, XP_BOOK_SECTION_PAGE_PARAGRAPH, page);
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  return sectionQuery(filePath, XP_BOOK_SECTION_LIST_PAGE, liIndex);
}

std::optional<uint32_t> Section::getVisibleTextOffsetForPage(const uint16_t page) const {
  HalFile file;
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  const auto records = buildRecords();
  uint32_t offset;
  if (!xp_book_section_page_visible(&io, &records, page, &offset)) return std::nullopt;
  return offset;
}

std::optional<uint16_t> Section::getPageForVisibleTextOffset(const uint32_t offset, const bool first) const {
  HalFile file;
  SectionFileContext context{file, filePath};
  const auto io = context.services();
  const auto records = buildRecords();
  uint32_t page;
  if (!xp_book_section_visible(&io, &records, offset, first, &page)) return std::nullopt;
  return page;
}

bool Section::buildReachedVisibleTextOffset(const uint32_t offset) const {
  const auto records = buildRecords();
  return xp_book_section_reached(&records, offset);
}
