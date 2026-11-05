#include "Paragraph.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>

#include <algorithm>
#include <cstring>

#include "hyphenation/Hyphenator.h"

namespace {
void* allocateParagraphBytes(size_t bytes, uint8_t) {
  // Rust returns this allocation through releaseParagraphBytes.
  return makeUniqueNoThrow<uint8_t[]>(bytes).release();
}
void releaseParagraphBytes(void* data) { delete[] static_cast<uint8_t*>(data); }
const XpParaAllocator ALLOCATOR = {
    allocateParagraphBytes, releaseParagraphBytes,
    [](void*, uint32_t event, uint32_t bytes, uint32_t, uint32_t, uint32_t) {
      switch (static_cast<XpParaDiagnostic>(event)) {
        case XpParaDiagnostic::NfcOffsets:
          LOG_ERR("PTX", "OOM: NFC source offsets (%u bytes)", static_cast<unsigned>(bytes));
          break;
        case XpParaDiagnostic::WordArena:
          LOG_ERR("PTX", "OOM: dropping paragraph text (arena chunk alloc failed)");
          break;
        case XpParaDiagnostic::LineArena:
          LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
          break;
        case XpParaDiagnostic::Rescued:
          LOG_DBG("WST", "Cache eviction rescued a %u-byte chunk allocation", static_cast<unsigned>(bytes));
          break;
        case XpParaDiagnostic::OtherStorage:
          LOG_ERR("PTX", "OOM: paragraph storage (%zu bytes)", static_cast<size_t>(bytes));
          break;
      }
    },
    [](size_t bytes) { freeink::MemoryManager::instance().ensureFree(bytes); }};
}  // namespace

const XpParaAllocator* paragraphAllocator() { return &ALLOCATOR; }

namespace {
XpParaBlock paraBlock(const BlockStyle& value) {
  XpParaBlock result{};
  result.character_spacing = value.characterSpacing;
  result.margin_top = value.marginTop;
  result.margin_bottom = value.marginBottom;
  result.margin_left = value.marginLeft;
  result.margin_right = value.marginRight;
  result.padding_top = value.paddingTop;
  result.padding_bottom = value.paddingBottom;
  result.padding_left = value.paddingLeft;
  result.padding_right = value.paddingRight;
  result.text_indent = value.textIndent;
  result.text_indent_defined = value.textIndentDefined;
  result.text_align_defined = value.textAlignDefined;
  result.rtl = value.isRtl;
  result.direction_defined = value.directionDefined;
  result.font_scale = value.fontScale;
  result.font_scale_defined = value.fontScaleDefined;
  result.font_id = value.fontId;
  result.break_before = value.pageBreakBefore;
  result.break_after = value.pageBreakAfter;
  result.keep_with_next = value.keepWithNext;
  result.alignment = static_cast<uint8_t>(value.alignment);
  return result;
}
BlockStyle paraBlock(const XpParaBlock& value) {
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

XpParaSpan span(std::string_view text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size())};
}
uint8_t startsRtl(XpParaSpan text, int32_t depth) {
  return BidiUtils::startsWithRtl(std::string_view(reinterpret_cast<const char*>(text.data), text.len), depth);
}
const GfxRenderer& renderer(void* context) { return *static_cast<const GfxRenderer*>(context); }
int32_t advance(void* context, int32_t font, XpParaSpan text, uint8_t style, int8_t tracking) {
  return renderer(context).getTextAdvanceX(font, text.len ? reinterpret_cast<const char*>(text.data) : "",
                                           static_cast<EpdFontFamily::Style>(style), tracking);
}
void prepare(void* context, int32_t font, const XpParaSpan* spans, uint32_t count, uint8_t space, uint8_t hyphen,
             uint8_t styles, uint8_t small) {
  std::vector<const char*> segments;
  std::vector<size_t> lengths;
  segments.reserve(count);
  lengths.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    segments.push_back(reinterpret_cast<const char*>(spans[i].data));
    lengths.push_back(spans[i].len);
  }
  renderer(context).ensureSdCardFontReady(font, segments.data(), lengths.data(), count, space, hyphen, styles, small);
}
uint8_t visualOrder(XpParaSpan text, const uint16_t* offsets, uint32_t count, uint8_t rtl, uint16_t* order) {
  return BidiUtils::computeVisualWordOrder({reinterpret_cast<const char*>(text.data), text.len}, {offsets, count}, rtl,
                                           {order, count});
}
}  // namespace
Paragraph::Paragraph(bool hyphenation, bool focus, const BlockStyle& style, uint8_t indent) {
  const auto wire = paraBlock(style);
  xp_para_init(handle(), paragraphAllocator(), &wire, hyphenation, focus, indent, startsRtl);
}
void Paragraph::addWord(std::string_view word, EpdFontFamily::Style style, bool underline, bool attach, uint32_t offset,
                        uint8_t link, uint8_t slot, bool noSpace) {
  xp_para_add_word(handle(), span(word), style, underline, attach, offset, link, slot, noSpace);
}
uint8_t Paragraph::addLinkTarget(const char* href) {
  return href ? xp_para_add_link(handle(), span({href, strnlen(href, 256)})) : 0;
}
bool Paragraph::linkTargetMatches(uint8_t link, const char* href) const {
  return href && xp_para_link_matches(handle(), link, span(href));
}
void Paragraph::setRubyForWordAt(size_t index, const std::string& text) {
  xp_para_set_ruby(handle(), index, span(text));
}
void Paragraph::setRubyGroupAt(size_t index, size_t count, const std::string& text) {
  xp_para_set_ruby_group(handle(), index, count, span(text));
}
std::string Paragraph::getRubyTextAt(size_t index) const {
  const auto text = xp_para_ruby(handle(), index);
  return {reinterpret_cast<const char*>(text.data), text.len};
}
BlockStyle Paragraph::getBlockStyle() const {
  XpParaBlock style{};
  xp_para_get_style(handle(), &style);
  return paraBlock(style);
}
void Paragraph::setBlockStyle(const BlockStyle& style) {
  const auto wire = paraBlock(style);
  xp_para_set_style(handle(), &wire);
}
void Paragraph::layoutLines(const GfxRenderer& source, int fontId, uint16_t viewportWidth, void* context,
                            uint8_t (*processLine)(void*, const XpParaLine*), bool includeLastLine,
                            int8_t characterSpacing, uint8_t wordSpacingPercent, size_t maxLines,
                            const bool* stopRequested) {
  XpParaMetrics metrics{const_cast<GfxRenderer*>(&source),
                        nullptr,
#ifdef XP_NATIVE_TEXT_MEASUREMENT
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        nullptr,
                        prepare,
                        nullptr,
                        nullptr,
#else
                        advance,
                        [](void* c, int32_t f, uint8_t s) -> int32_t {
                          return renderer(c).getSpaceWidth(f, static_cast<EpdFontFamily::Style>(s));
                        },
                        [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s, int8_t t) -> int32_t {
                          return renderer(c).getKerning(f, a, b, static_cast<EpdFontFamily::Style>(s), t);
                        },
                        [](void* c, int32_t f, uint32_t a, uint32_t b, uint8_t s) -> int32_t {
                          return renderer(c).getSpaceAdvance(f, a, b, static_cast<EpdFontFamily::Style>(s));
                        },
                        [](void* c, int32_t f) -> int32_t { return renderer(c).getFontAscenderSize(f); },
                        [](void* c, int32_t f) -> uint8_t { return renderer(c).isSdCardFont(f); },
                        prepare,
                        startsRtl,
                        visualOrder,
#endif
#ifdef XP_NATIVE_TEXT_MEASUREMENT
                        [](void* ctx, XpNativeProvider* out) -> uint8_t {
                          *out = renderer(ctx).textProvider();
                          return true;
                        },
#else
                        nullptr,
#endif
                        nullptr};
  XpParaLayout options{fontId,
                       viewportWidth,
                       static_cast<uint8_t>(includeLastLine),
                       characterSpacing,
                       wordSpacingPercent,
                       static_cast<uint32_t>(std::min<size_t>(maxLines, UINT32_MAX)),
                       Hyphenator::preferredLanguageId(),
                       reinterpret_cast<const uint8_t*>(stopRequested),
                       context,
                       processLine};
  xp_para_layout(handle(), &metrics, &options);
}

#ifdef XP_PAGE_CODEC_TEST
void Paragraph::layoutAndExtractLines(const GfxRenderer& source, int fontId, uint16_t viewportWidth,
                                      const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                                      bool includeLastLine, int8_t characterSpacing, uint8_t wordSpacingPercent,
                                      size_t maxLines, const bool* stopRequested) {
  layoutLines(
      source, fontId, viewportWidth, const_cast<void*>(static_cast<const void*>(&processLine)),
      [](void* raw, const XpParaLine* line) -> uint8_t {
        const auto& process = *static_cast<const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>*>(raw);
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
            std::span<const uint16_t>(line->offsets, line->count),
            std::span<const int16_t>(line->positions, line->count),
            std::span<const EpdFontFamily::Style>(reinterpret_cast<const EpdFontFamily::Style*>(line->styles),
                                                  line->count),
            std::span<const uint8_t>(line->boundaries, line->boundaries ? line->count : 0),
            std::span<const uint16_t>(line->suffixes, line->suffixes ? line->count : 0), paraBlock(*line->block),
            std::move(ruby), std::span<const XpParaLink>(line->links, line->link_count),
            std::span<const TextBlock::SourceRange>(reinterpret_cast<const TextBlock::SourceRange*>(line->sources),
                                                    line->count),
            line->paragraph_start);
        int32_t fonts[TextBlock::MAX_WORD_FONTS];
        memcpy(fonts, line->fonts, sizeof(fonts));
        if (!block || !block->valid() || (line->font_slots && !block->setWordFonts(line->font_slots, fonts))) {
          LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
          return false;
        }
        process(std::move(block), line->visible_offset);
        return true;
      },
      includeLastLine, characterSpacing, wordSpacingPercent, maxLines, stopRequested);
}

#endif
