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

void* createLine(void*, const XpParaLine* line) {
  std::vector<std::string> ruby;
  bool hasRuby = false;
  for (uint32_t i = 0; i < line->count; ++i) hasRuby |= line->ruby[i].len != 0;
  if (hasRuby) {
    ruby.reserve(line->count);
    for (uint32_t i = 0; i < line->count; ++i)
      ruby.emplace_back(reinterpret_cast<const char*>(line->ruby[i].data), line->ruby[i].len);
  }
  auto block = makeUniqueNoThrow<TextBlock>(
      TextBlock::BorrowedLayout{},
      std::span<const char>(reinterpret_cast<const char*>(line->text.data), line->text.len),
      std::span<const uint16_t>(line->offsets, line->count), std::span<const int16_t>(line->positions, line->count),
      std::span<const EpdFontFamily::Style>(reinterpret_cast<const EpdFontFamily::Style*>(line->styles), line->count),
      std::span<const uint8_t>(line->boundaries, line->boundaries ? line->count : 0),
      std::span<const uint16_t>(line->suffixes, line->suffixes ? line->count : 0), chapterBlock(*line->block),
      std::move(ruby), std::span<const XpParaLink>(line->links, line->link_count),
      std::span<const TextBlock::SourceRange>(reinterpret_cast<const TextBlock::SourceRange*>(line->sources),
                                              line->count),
      line->paragraph_start);
  int32_t fonts[TextBlock::MAX_WORD_FONTS];
  memcpy(fonts, line->fonts, sizeof(fonts));
  if (!block || !block->valid() || (line->font_slots && !block->setWordFonts(line->font_slots, fonts))) {
    LOG_ERR("PTX", "OOM: extracted line");
    return nullptr;
  }
  return block.release();
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
    visualOrder};
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
    [](void* ctx, int32_t font, uint32_t cp, uint8_t style, int32_t* advance, int32_t* top) -> uint8_t {
      int32_t measuredAdvance = 0;
      int measuredTop = 0;
      const bool found = static_cast<ChapterPageAssembler*>(ctx)->renderer.getCodepointMetrics(
          font, cp, static_cast<EpdFontFamily::Style>(style), measuredAdvance, measuredTop);
      *advance = measuredAdvance;
      *top = measuredTop;
      return found;
    },
    createLine,
    [](void* ctx, const void* raw, const XpChapterAssemblyOptions* options, bool row) -> XpChapterLineMetrics {
      const auto& line = *static_cast<const TextBlock*>(raw);
      const auto& renderer = static_cast<ChapterPageAssembler*>(ctx)->renderer;
      const int font = line.blockFontId(options->font);
      const auto rubyHeight = [&]() { return line.getRubyShift(renderer.getFontAscenderSize(font)); };
      const auto baseHeight = [&]() {
        return renderer.getLineHeight(font, options->compression) + line.extraAscent(renderer, options->font);
      };
      int height, ruby;
      if (row) {
        height = baseHeight();
        ruby = rubyHeight();
      } else {
        ruby = rubyHeight();
        height = baseHeight();
      }
      return {line.getBlockStyle().leftInset(), static_cast<uint32_t>(line.wordCount()), height, ruby};
    },
    [](void* raw, int16_t left) {
      auto* line = static_cast<TextBlock*>(raw);
      auto style = line->getBlockStyle();
      style.marginLeft = left;
      line->setBlockStyle(style);
    },
    [](void* ctx, void* raw, uint32_t* count) -> const XpChapterLineLink* {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      self->lineLinks_ = static_cast<TextBlock*>(raw)->takeLinkSpans();
      static_assert(sizeof(TextBlock::LinkSpan) == sizeof(XpChapterLineLink));
      *count = self->lineLinks_.count;
      return reinterpret_cast<const XpChapterLineLink*>(self->lineLinks_.data.get());
    },
    [](void* ctx, const XpChapterElement* elements, uint32_t count, const XpChapterLink* links, uint32_t linkCount,
       const XpChapterFootnote* footnotes, uint32_t footnoteCount, XpChapterPosition position) -> uint8_t {
      return static_cast<ChapterPageAssembler*>(ctx)->completePage(elements, count, links, linkCount, footnotes,
                                                                   footnoteCount, position);
    },
    [](void* ctx, XpChapterSpan src, int32_t* counter, bool* popup, uint16_t* width, uint16_t* height) -> uint8_t {
      return static_cast<ChapterPageAssembler*>(ctx)->probeImage(src, *counter, *popup, *width, *height);
    },
    [](void* ctx, int32_t width, int32_t height) -> void* {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      return makeUniqueNoThrow<ImageBlock>(self->imageCache_, self->imageSource_, width, height).release();
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
};

bool ChapterPageAssembler::ensureSemantics() {
  if (chapter_) return true;
  const auto options = chapterOptions();
  return xp_chapter_create(&chapter_, &sink_, this, cssParser ? cssParser->rustHandle() : nullptr, &options) == 0;
}

extern "C" void xp_chapter_release_block(void* block) { delete static_cast<Block*>(block); }
