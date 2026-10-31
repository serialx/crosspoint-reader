#include <BidiUtils.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <Utf8.h>
#include <xp_book.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <iterator>
#include <new>
#include <optional>

#include "../../../expat/expat.h"
#include "ChapterHtmlSlimParser.h"
#include "ChapterPageAssembler.h"
#include "ChapterTableConstants.h"
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/VisibleTextUtils.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageDimsProbe.h"
#include "Epub/converters/ImageToFramebufferDecoder.h"
#include "Epub/htmlEntities.h"
#include "Epub/hyphenation/Hyphenator.h"

namespace {
[[gnu::cold, gnu::noinline]] void logParseError(const uint64_t result) {
  const auto error = static_cast<uint32_t>(result) >> 8;
  const auto line = static_cast<uint32_t>(result >> 32);
  [[maybe_unused]] const char* message = XML_ErrorString(static_cast<XML_Error>(error));
  if (static_cast<uint8_t>(result) == 1) {
    LOG_DBG("EHP", "Ignoring trailing data after </html>: %s", message);
  } else {
    LOG_ERR("EHP", "Parse error at line %lu:\n%s", static_cast<unsigned long>(line), message);
  }
}
}  // namespace

struct ChapterHtmlSlimParser::PolicyContext {
  ChapterHtmlSlimParser& parser;
  uint64_t result = 0;
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<PolicyContext*>(raw);
    auto& self = c.parser;
    switch (operation) {
      case XP_BOOK_PARSE_ENSURE: {
        return self.ensureSemantics();
      }
      case XP_BOOK_PARSE_RESET_STOP: {
        self.stopRequested = false;
        break;
      }
      case XP_BOOK_PARSE_ALLOCATE_BUFFER: {
        self.parseBuffer_ = makeUniqueNoThrow<uint8_t[]>(argument);
        return bool(self.parseBuffer_);
      }
      case XP_BOOK_PARSE_SET_CAPACITY: {
        self.parseBufferCapacity_ = argument;
        break;
      }
      case XP_BOOK_PARSE_OPEN: {
        return Storage.openFileForRead("EHP", self.filepath, self.parseFile_);
      }
      case XP_BOOK_PARSE_HAS_POPUP: {
        return bool(self.popupFn);
      }
      case XP_BOOK_PARSE_SIZE: {
        return self.parseFile_.size();
      }
      case XP_BOOK_PARSE_POPUP: {
        self.popupFn();
        break;
      }
      case XP_BOOK_PARSE_START_CLOCK: {
        self.parseStartTime_ = millis();
        break;
      }
      case XP_BOOK_PARSE_STOPPED: {
        return self.stopRequested;
      }
      case XP_BOOK_PARSE_READ: {
        return self.parseFile_.read(self.parseBuffer_.get(), argument);
      }
      case XP_BOOK_PARSE_AVAILABLE: {
        return self.parseFile_.available();
      }
      case XP_BOOK_PARSE_RELEASE_BUFFER: {
        self.parseBuffer_.reset();
        break;
      }
      case XP_BOOK_PARSE_IS_OPEN: {
        return self.parseFile_.isOpen();
      }
      case XP_BOOK_PARSE_CLOSE: {
        self.parseFile_.close();
        break;
      }
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t) {
    auto& c = *static_cast<PolicyContext*>(raw);
    switch (event) {
      case XP_BOOK_PARSE_EVENT_PARSER_OOM:
        LOG_ERR("EHP", "Couldn't allocate memory for parser");
        break;
      case XP_BOOK_PARSE_EVENT_BUFFER_OOM:
        LOG_ERR("EHP", "Couldn't allocate memory for buffer");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_OOM:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_AFTER_FEED:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_BEFORE_FINISH:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_LAYOUT_AFTER_FINISH:
        LOG_ERR("EHP", "Text layout dropped content (OOM); failing section build");
        break;
      case XP_BOOK_PARSE_EVENT_READ_ERROR:
        LOG_ERR("EHP", "File read error");
        break;
      case XP_BOOK_PARSE_EVENT_PARSE_ERROR:
        logParseError(c.result);
        break;
      case XP_BOOK_PARSE_EVENT_TIME:
        LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - c.parser.parseStartTime_);
        break;
    }
  }
};
uint32_t ChapterHtmlSlimParser::parseOperation(const uint32_t action) {
  PolicyContext context{*this};
  const XpBookServices io{&context, PolicyContext::operation, nullptr, PolicyContext::diagnostic};
  return xp_book_parse(
      &io, &chapter_,
      [](void* raw) -> const uint8_t* { return static_cast<PolicyContext*>(raw)->parser.parseBuffer_.get(); },
      &context.result, action);
}
ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }
bool ChapterHtmlSlimParser::beginParse() { return parseOperation(0); }
ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {
  return static_cast<ParseStatus>(parseOperation(1));
}
void ChapterHtmlSlimParser::abortParse() { parseOperation(2); }
bool ChapterHtmlSlimParser::finishParse() { return parseOperation(3); }
bool ChapterHtmlSlimParser::parseAndBuildPages() { return parseOperation(4); }
void ChapterHtmlSlimParser::requestStop() { stopRequested = true; }

void ChapterHtmlSlimParser::withTableFile(XpChapterState* state, XpChapterUseReader use) {
  struct Context {
    ChapterHtmlSlimParser& parser;
    HalFile file;
  } context{*this, {}};
  const XpChapterReader reader{&context,
                               [](void* raw) -> uint8_t {
                                 auto& c = *static_cast<Context*>(raw);
                                 return Storage.openFileForRead("EHP", c.parser.filepath, c.file);
                               },
                               [](void* raw, uint64_t offset) -> uint8_t {
                                 return static_cast<Context*>(raw)->file.seek(static_cast<size_t>(offset));
                               },
                               [](void* raw, uint8_t* bytes, uint32_t capacity) -> uint32_t {
                                 return static_cast<Context*>(raw)->file.read(bytes, capacity);
                               }};
  use(state, &reader);
}

XpChapterSpan ChapterHtmlSlimParser::entityBuffer(const uint32_t length) {
  if (length != 0) {
    auto buffer = makeUniqueNoThrow<uint8_t[]>(length);
    if (!buffer) return {nullptr, 0};
    parseBuffer_ = std::move(buffer);
    parseBufferCapacity_ = length;
  }
  return {parseBuffer_.get(), static_cast<uint32_t>(parseBufferCapacity_)};
}

struct ChapterPageAssembler::ProbeContext {
  ChapterPageAssembler& owner;
  XpChapterSpan src;
  int32_t& counter;
  bool& popup;
  uint16_t& width;
  uint16_t& height;
  std::string resolved, cached;
  XpBookTextSpan resolvedView{};
  ImageDimensions dimensions{0, 0};
  std::optional<ImageDimsProbe> probe;
  std::optional<HalFile> file;
  ImageToFramebufferDecoder* decoder = nullptr;
  ProbeContext(ChapterPageAssembler& owner, XpChapterSpan src, int32_t& counter, bool& popup, uint16_t& width,
               uint16_t& height)
      : owner(owner), src(src), counter(counter), popup(popup), width(width), height(height) {}
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<ProbeContext*>(raw);
    auto& self = c.owner;
    switch (operation) {
      case XP_BOOK_PROBE_RESOLVE: {
        c.resolved = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(
            self.contentBase + std::string(reinterpret_cast<const char*>(c.src.data), c.src.len)));
        c.resolvedView = {reinterpret_cast<const uint8_t*>(c.resolved.data()),
                          static_cast<uint32_t>(c.resolved.size())};
        break;
      }
      case XP_BOOK_PROBE_SUPPORTED: {
        return ImageDecoderFactory::isFormatSupported(c.resolved);
      }
      case XP_BOOK_PROBE_PREPARE_CACHE: {
        std::string ext;
        ext = c.resolved.substr(argument);
        c.cached = self.imageBasePath + std::to_string(c.counter) + ext;
        break;
      }
      case XP_BOOK_PROBE_PROBE: {
        c.probe.emplace();
        self.epub->readItemContentsToStream(c.resolved, *c.probe, 1024, true);
        return c.probe->getDimensions(c.dimensions);
      }
      case XP_BOOK_PROBE_RETRY_PROBE: {
        GfxRenderer::FrameBufferLoan loan(self.renderer);
        ImageDimsProbe probe;
        self.epub->readItemContentsToStream(c.resolved, probe, 1024, true);
        return probe.getDimensions(c.dimensions);
      }
      case XP_BOOK_PROBE_HAS_POPUP: {
        return bool(self.popupFn);
      }
      case XP_BOOK_PROBE_POPUP_SHOWN: {
        return c.popup;
      }
      case XP_BOOK_PROBE_SET_POPUP: {
        c.popup = true;
        break;
      }
      case XP_BOOK_PROBE_POPUP: {
        self.popupFn();
        break;
      }
      case XP_BOOK_PROBE_OPEN: {
        c.file.emplace();
        return Storage.openFileForWrite("EHP", c.cached, *c.file);
      }
      case XP_BOOK_PROBE_EXTRACT: {
        GfxRenderer::FrameBufferLoan loan(self.renderer);
        return self.epub->readItemContentsToStream(c.resolved, *c.file, 4096);
      }
      case XP_BOOK_PROBE_FLUSH: {
        c.file->flush();
        break;
      }
      case XP_BOOK_PROBE_CLOSE: {
        c.file->close();
        break;
      }
      case XP_BOOK_PROBE_DECODER: {
        c.decoder = ImageDecoderFactory::getDecoder(c.cached);
        return c.decoder != nullptr;
      }
      case XP_BOOK_PROBE_DIMENSIONS: {
        return c.decoder->getDimensions(c.cached, c.dimensions);
      }
      case XP_BOOK_PROBE_DELAY: {
        delay(argument);
        break;
      }
      case XP_BOOK_PROBE_STORE_SIZE: {
        c.width = c.dimensions.width;
        c.height = c.dimensions.height;
        break;
      }
      case XP_BOOK_PROBE_ACCEPT: {
        self.imageSource_ = std::move(c.resolved);
        self.imageCache_ = std::move(c.cached);
        break;
      }
      case XP_BOOK_PROBE_REMOVE: {
        return Storage.remove(c.cached.c_str());
      }
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t) {
    auto& c = *static_cast<ProbeContext*>(raw);
    switch (event) {
      case XP_BOOK_PROBE_EVENT_FOUND:
        LOG_DBG("EHP", "Found image: src=%.*s", static_cast<int>(c.src.len), reinterpret_cast<const char*>(c.src.data));
        break;
      case XP_BOOK_PROBE_EVENT_EXTRACT_FAILED:
        LOG_ERR("EHP", "Failed to extract image");
        break;
      case XP_BOOK_PROBE_EVENT_DIMENSIONS:
        LOG_DBG("EHP", "Image dimensions: %dx%d", c.dimensions.width, c.dimensions.height);
        break;
      case XP_BOOK_PROBE_EVENT_DIMENSIONS_FAILED:
        LOG_ERR("EHP", "Failed to get image dimensions");
        break;
    }
  }
};
uint8_t ChapterPageAssembler::probeImage(XpChapterSpan src, int32_t& counter, bool& popup, uint16_t& width,
                                         uint16_t& height) {
  ProbeContext context{*this, src, counter, popup, width, height};
  const XpBookServices io{&context, ProbeContext::operation, nullptr, ProbeContext::diagnostic};
  return xp_book_probe_image(&io, &context.resolvedView, &counter);
}

uint8_t ChapterPageAssembler::completeBytes(uint32_t operation, XpChapterSpan bytes, XpChapterPosition position) {
  switch (operation) {
    case XP_CHAPTER_HAS_BYTE_SINK:
      return bool(completePageBytesFn);
    case XP_CHAPTER_BYTE_SINK:
      completePageBytesFn(bytes, position.paragraph, position.list, position.visible);
      break;
    case XP_CHAPTER_PAGE_SINK: {
#ifdef XP_CHAPTER_SINK_FAULT_INJECTION
      extern bool chapterTestFailPageAllocation();
      if (chapterTestFailPageAllocation()) return 2;
#endif
      auto page = Page::fromBytes(bytes.data, bytes.len, position.visible);
      if (!page) {
        LOG_ERR("CHP", "OOM: decoded page");
        return 2;
      }
      completePageFn(std::move(page), position.paragraph, position.list, position.visible);
      break;
    }
  }
  return stopRequested ? 1 : 0;
}

ChapterPageAssembler::~ChapterPageAssembler() = default;

const XpParaAllocator* paragraphAllocator();
namespace {
template <class Wire>
BlockStyle chapterBlock(const Wire& value) {
  BlockStyle result;
  result.characterSpacing = value.character_spacing;
  result.marginTop = value.margin_top;
  result.marginBottom = value.margin_bottom;
  result.marginLeft = value.margin_left;
  result.marginRight = value.margin_right;
  result.paddingTop = value.padding_top;
  result.paddingBottom = value.padding_bottom;
  result.paddingLeft = value.padding_left;
  result.paddingRight = value.padding_right;
  result.textIndent = value.text_indent;
  result.textIndentDefined = value.text_indent_defined;
  result.textAlignDefined = value.text_align_defined;
  result.isRtl = value.rtl;
  result.directionDefined = value.direction_defined;
  result.fontScale = value.font_scale;
  result.fontScaleDefined = value.font_scale_defined;
  result.fontId = value.font_id;
  result.pageBreakBefore = value.break_before;
  result.pageBreakAfter = value.break_after;
  result.keepWithNext = value.keep_with_next;
  result.alignment = static_cast<CssTextAlign>(value.alignment);
  return result;
}
uint8_t startsRtl(XpParaSpan text, int32_t depth) {
  return BidiUtils::startsWithRtl(std::string_view(reinterpret_cast<const char*>(text.data), text.len), depth);
}
}  // namespace
int32_t ChapterPageAssembler::textAdvance(void* context, int32_t font, XpParaSpan text, uint8_t style,
                                          int8_t tracking) {
  return metricRenderer(context).getTextAdvanceX(font, text.len ? reinterpret_cast<const char*>(text.data) : "",
                                                 static_cast<EpdFontFamily::Style>(style), tracking);
}
void ChapterPageAssembler::prepareText(void* context, int32_t font, const XpParaSpan* spans, uint32_t count,
                                       uint8_t space, uint8_t hyphen, uint8_t styles, uint8_t small) {
  std::vector<const char*> segments;
  std::vector<size_t> lengths;
  segments.reserve(count);
  lengths.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    segments.push_back(reinterpret_cast<const char*>(spans[i].data));
    lengths.push_back(spans[i].len);
  }
  metricRenderer(context).ensureSdCardFontReady(font, segments.data(), lengths.data(), count, space, hyphen, styles,
                                                small);
}
namespace {
uint8_t visualOrder(XpParaSpan text, const uint16_t* offsets, uint32_t count, uint8_t rtl, uint16_t* order) {
  return BidiUtils::computeVisualWordOrder({reinterpret_cast<const char*>(text.data), text.len}, {offsets, count}, rtl,
                                           {order, count});
}

}  // namespace

XpChapterOptions ChapterPageAssembler::chapterOptions() const {
  return {paragraphAlignment,
          static_cast<uint8_t>(embeddedStyle),
          imageRendering,
          static_cast<uint8_t>(cssParser != nullptr),
          {fontId, lineCompression, viewportWidth, viewportHeight, extraParagraphSpacing, paragraphAlignment,
           paragraphIndentSpaces, characterSpacing, wordSpacingPercent, hyphenationEnabled, focusReadingEnabled}};
}

const XpParaMetrics ChapterPageAssembler::metrics_{
    nullptr,
    nullptr,  // Layout tracing is installed only by the host instrumenter.
#ifdef XP_NATIVE_TEXT_MEASUREMENT
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    prepareText,
    nullptr,
    nullptr,
#else
    textAdvance,
    [](void* c, int32_t f, uint8_t s) -> int32_t {
      return metricRenderer(c).getSpaceWidth(f, static_cast<EpdFontFamily::Style>(s));
    },
    [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s, int8_t t) -> int32_t {
      return metricRenderer(c).getKerning(f, a, b, static_cast<EpdFontFamily::Style>(s), t);
    },
    [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s) -> int32_t {
      return metricRenderer(c).getSpaceAdvance(f, a, b, static_cast<EpdFontFamily::Style>(s));
    },
    [](void* c, int32_t f) -> int32_t { return metricRenderer(c).getFontAscenderSize(f); },
    [](void* c, int32_t f) -> uint8_t { return metricRenderer(c).isSdCardFont(f); },
    prepareText,
    startsRtl,
    visualOrder,
#endif
#ifdef XP_NATIVE_TEXT_MEASUREMENT
    [](void* ctx, XpNativeProvider* out) -> uint8_t {
      *out = metricRenderer(ctx).textProvider();
      return true;
    },
#else
    nullptr,
#endif
    nullptr};
const XpChapterSink ChapterPageAssembler::sink_ = {
    paragraphAllocator,
    &metrics_,
    Hyphenator::preferredLanguageId,
    []() { freeink::MemoryManager::instance().ensureFree(4 * 1024); },
    [](void*, XpChapterSpan spelling) -> XpChapterSpan {
      const char* value = lookupHtmlEntity(reinterpret_cast<const char*>(spelling.data), spelling.len);
      return {reinterpret_cast<const uint8_t*>(value), value ? static_cast<uint32_t>(strlen(value)) : 0};
    },
    [](void* ctx, uint32_t length) { return static_cast<ChapterPageAssembler*>(ctx)->entityBuffer(length); },
    [](void* ctx, int32_t font, float scale) -> int32_t {
      return static_cast<ChapterPageAssembler*>(ctx)->renderer.resolveFontVariant(font, scale);
    },
    [](void* ctx, int32_t font, float compression) -> int32_t {
      return static_cast<ChapterPageAssembler*>(ctx)->renderer.getLineHeight(font, compression);
    },
#ifdef XP_NATIVE_TEXT_MEASUREMENT
    nullptr,
#else
    [](void* ctx, int32_t font, uint32_t cp, uint8_t style, int32_t* advance, int32_t* top) -> uint8_t {
      int32_t measuredAdvance = 0;
      int measuredTop = 0;
      const bool found = static_cast<ChapterPageAssembler*>(ctx)->renderer.getCodepointMetrics(
          font, cp, static_cast<EpdFontFamily::Style>(style), measuredAdvance, measuredTop);
      *advance = measuredAdvance;
      *top = measuredTop;
      return found;
    },
#endif
    [](void* ctx, int32_t font, int32_t fallback) -> int32_t {
      const auto& renderer = static_cast<ChapterPageAssembler*>(ctx)->renderer;
      return font == fallback || renderer.ensureFontLoaded(font) ? font : fallback;
    },
    [](void* ctx, uint32_t operation, XpChapterSpan bytes, XpChapterPosition position) -> uint8_t {
      return static_cast<ChapterPageAssembler*>(ctx)->completeBytes(operation, bytes, position);
    },
    [](void* ctx, XpChapterSpan src, int32_t* counter, bool* popup, uint16_t* width, uint16_t* height) -> uint8_t {
      return static_cast<ChapterPageAssembler*>(ctx)->probeImage(src, *counter, *popup, *width, *height);
    },
    [](void* ctx, XpChapterSpan* cache, XpChapterSpan* source) {
      const auto* self = static_cast<ChapterPageAssembler*>(ctx);
      *cache = {reinterpret_cast<const uint8_t*>(self->imageCache_.data()),
                static_cast<uint32_t>(self->imageCache_.size())};
      *source = {reinterpret_cast<const uint8_t*>(self->imageSource_.data()),
                 static_cast<uint32_t>(self->imageSource_.size())};
    },
    [](void* ctx) -> XpChapterSpan {
      const auto& id = static_cast<ChapterPageAssembler*>(ctx)->pendingAnchorId;
      return {reinterpret_cast<const uint8_t*>(id.data()), static_cast<uint32_t>(id.size())};
    },
    [](void* ctx, XpChapterSpan id) {
      static_cast<ChapterPageAssembler*>(ctx)->pendingAnchorId.assign(reinterpret_cast<const char*>(id.data), id.len);
    },
    [](void* ctx, uint32_t index) -> XpChapterSpan {
      const auto& anchors = static_cast<ChapterPageAssembler*>(ctx)->tocAnchors;
      if (index >= anchors.size()) return {nullptr, 0};
      const auto& id = anchors[index];
      return {reinterpret_cast<const uint8_t*>(id.data()), static_cast<uint32_t>(id.size())};
    },
    [](void* ctx) -> uint32_t { return static_cast<ChapterPageAssembler*>(ctx)->anchorData.size(); },
    [](void* ctx, uint16_t page) {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      self->anchorData.emplace_back(std::move(self->pendingAnchorId), page);
      self->pendingAnchorId.clear();
    },
    [](void* ctx, XpChapterState* state, XpChapterUseReader use) {
      static_cast<ChapterPageAssembler*>(ctx)->withTableFile(state, use);
    },
    [](void*, const XpChapterTrace* record) { (void)record; },
    [](void*, XpChapterSpan record) { (void)record; },
    [](void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t, const uint8_t* text, uint32_t length) {
      switch (static_cast<XpChapterDiagnostic>(event)) {
        case XpChapterDiagnostic::BorderBox:
          LOG_ERR("EHP", "OOM: PageBorderBox");
          break;
        case XpChapterDiagnostic::DropCap:
          LOG_ERR("EHP", "OOM: PageDropCap");
          break;
        case XpChapterDiagnostic::Paragraph:
          LOG_ERR("EHP", "OOM: ParsedText");
          break;
        case XpChapterDiagnostic::HorizontalRule:
          LOG_ERR("EHP", "Failed to create PageHorizontalRule");
          break;
        case XpChapterDiagnostic::RowSeparator:
          LOG_ERR("EHP", "OOM: table row separator");
          break;
        case XpChapterDiagnostic::CellBorder:
          LOG_ERR("EHP", "OOM: table cell border");
          break;
        case XpChapterDiagnostic::TableCell:
          LOG_ERR("EHP", "OOM: table cell");
          break;
        case XpChapterDiagnostic::ImageBlock:
          LOG_ERR("EHP", "Failed to create ImageBlock");
          break;
        case XpChapterDiagnostic::PageImage:
          LOG_ERR("EHP", "Failed to create PageImage");
          break;
        case XpChapterDiagnostic::CharacterParagraph:
          LOG_ERR("EHP", "OOM: text block for character data");
          break;
        case XpChapterDiagnostic::AfterTableParagraph:
          LOG_ERR("EHP", "OOM: text block after table");
          break;
        case XpChapterDiagnostic::PageLine:
          LOG_ERR("EHP", "OOM: PageLine");
          break;
        case XpChapterDiagnostic::NoParagraph:
          LOG_ERR("EHP", "!! No text block to make pages for !!");
          break;
        case XpChapterDiagnostic::SoftFlush:
          LOG_DBG("EHP", "Text block soft flush (%u words)", static_cast<unsigned>(a));
          break;
        case XpChapterDiagnostic::NestedTable:
          LOG_DBG("EHP", "nested table flattened into enclosing cell");
          break;
        case XpChapterDiagnostic::ImageCssBoth:
          LOG_DBG("EHP", "Display size from CSS height+width: %dx%d", static_cast<int32_t>(a), static_cast<int32_t>(b));
          break;
        case XpChapterDiagnostic::ImageCssHeight:
          LOG_DBG("EHP", "Display size from CSS height: %dx%d", static_cast<int32_t>(a), static_cast<int32_t>(b));
          break;
        case XpChapterDiagnostic::ImageCssWidth:
          LOG_DBG("EHP", "Display size from CSS width: %dx%d", static_cast<int32_t>(a), static_cast<int32_t>(b));
          break;
        case XpChapterDiagnostic::ImageScale: {
          float scale;
          memcpy(&scale, &c, sizeof(scale));
          LOG_DBG("EHP", "Display size: %dx%d (scale %.2f)", static_cast<int32_t>(a), static_cast<int32_t>(b), scale);
          break;
        }
        case XpChapterDiagnostic::DroppedLink:
          LOG_DBG("EHP", "Dropped page link: %.*s", static_cast<int>(std::min<uint32_t>(length, 48)),
                  reinterpret_cast<const char*>(text));
          break;
      }
    },
};

bool ChapterPageAssembler::ensureSemantics() {
  if (chapter_) return true;
  const auto options = chapterOptions();
  return xp_chapter_create(&chapter_, &sink_, this, cssParser ? cssParser->rustHandle() : nullptr, &options) == 0;
}
