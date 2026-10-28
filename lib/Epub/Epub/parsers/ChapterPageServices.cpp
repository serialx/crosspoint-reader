#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>

#include <algorithm>
#include <cstring>

#include "ChapterPageAssembler.h"
#include "Epub/htmlEntities.h"
#include "Epub/hyphenation/Hyphenator.h"
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
    [](void* ctx, XpChapterSpan bytes, XpChapterPosition position) -> uint8_t {
      return static_cast<ChapterPageAssembler*>(ctx)->completeBytes(bytes, position);
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
    [](void* ctx, XpChapterSpan id) -> uint8_t {
      const auto& anchors = static_cast<ChapterPageAssembler*>(ctx)->tocAnchors;
      const std::string_view value(reinterpret_cast<const char*>(id.data), id.len);
      return std::find(anchors.begin(), anchors.end(), value) != anchors.end();
    },
    [](void* ctx) -> uint32_t { return static_cast<ChapterPageAssembler*>(ctx)->anchorData.size(); },
    [](void* ctx, uint16_t page) {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      if (!self->pendingAnchorId.empty()) {
        self->anchorData.emplace_back(std::move(self->pendingAnchorId), page);
        self->pendingAnchorId.clear();
      }
    },
    [](void* ctx, XpChapterState* state) {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      auto* previous = self->activeSemantics_;
      self->activeSemantics_ = state;
      self->measureTableAtCurrentPosition();
      self->activeSemantics_ = previous;
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
