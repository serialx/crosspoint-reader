#include <algorithm>
#include <cstring>
#include <string_view>

#include "ChapterPageAssembler.h"
#include "Epub/htmlEntities.h"

namespace {
std::string chapterString(XpChapterSpan span) {
  return span.len ? std::string(reinterpret_cast<const char*>(span.data), span.len) : std::string();
}
XpChapterBlock chapterBlock(const BlockStyle& value) {
  XpChapterBlock result{};
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
BlockStyle chapterBlock(const XpChapterBlock& value) {
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
// Assembly consumes only image lengths and box edges from the CSS wire value.
CssStyle chapterCss(const uint8_t* wire) {
  CssStyle css;
  memcpy(&css.imageHeight.value, wire + 50, 4);
  css.imageHeight.unit = static_cast<CssUnit>(wire[54]);
  memcpy(&css.imageWidth.value, wire + 55, 4);
  css.imageWidth.unit = static_cast<CssUnit>(wire[59]);
  const uint32_t flags =
      uint32_t(wire[84]) | uint32_t(wire[85]) << 8 | uint32_t(wire[86]) << 16 | uint32_t(wire[87]) << 24;
  css.defined.imageHeight = (flags & (1u << 13)) != 0;
  css.defined.imageWidth = (flags & (1u << 14)) != 0;
  CssBorderSide* sides[] = {&css.borderTop, &css.borderRight, &css.borderBottom, &css.borderLeft};
  for (size_t i = 0; i < 4; ++i) *sides[i] = {wire[71 + i * 2], static_cast<CssBorderStyle>(wire[72 + i * 2])};
  css.defined.borderTop = (flags & (1u << 23)) != 0;
  css.defined.borderRight = (flags & (1u << 24)) != 0;
  css.defined.borderBottom = (flags & (1u << 25)) != 0;
  css.defined.borderLeft = (flags & (1u << 26)) != 0;
  css.borderWidthsDefined = wire[79] & 15;
  css.borderStylesDefined = wire[79] >> 4;
  css.defined.shaded = (flags & (1u << 27)) != 0;
  css.shaded = wire[80] != 0;
  return css;
}
}  // namespace

bool ChapterPageAssembler::ensureSemantics() {
  if (chapter_) {
    bindParagraph();
    return true;
  }
  const XpChapterOptions options{paragraphAlignment, static_cast<uint8_t>(embeddedStyle), imageRendering,
                                 static_cast<uint8_t>(cssParser != nullptr)};
  const bool created =
      xp_chapter_create(&chapter_, &services_, this, cssParser ? cssParser->rustHandle() : nullptr, &options) == 0;
  if (created) bindParagraph();
  return created;
}
float ChapterPageAssembler::semanticInlineScale(const float fallback) const {
  if (!semanticQuery(XP_CHAPTER_QUERY_INLINE_VALID)) return fallback;
  const uint32_t bits = semanticQuery(XP_CHAPTER_QUERY_INLINE_SCALE);
  float scale;
  memcpy(&scale, &bits, sizeof(scale));
  return scale;
}
BlockStyle ChapterPageAssembler::semanticParentBlock() const {
  auto block = chapterBlock(BlockStyle{});
  xp_chapter_block(semanticState(), &block);
  return chapterBlock(block);
}
XpChapterDropCap ChapterPageAssembler::semanticDropCap() const {
  XpChapterDropCap cap{};
  cap.span_depth = -1;
  xp_chapter_drop_cap(semanticState(), &cap);
  return cap;
}
CssBorderSide ChapterPageAssembler::semanticTableBorder() const {
  const uint32_t packed = semanticQuery(XP_CHAPTER_QUERY_TABLE_BORDER);
  return {static_cast<uint8_t>(packed), static_cast<CssBorderStyle>(packed >> 8)};
}
bool ChapterPageAssembler::semanticAnchorStartsChapter() const {
  return std::find(tocAnchors.begin(), tocAnchors.end(), pendingAnchorId) != tocAnchors.end();
}
void ChapterPageAssembler::semanticRecordAnchor(const uint16_t page) {
  if (pendingAnchorId.empty()) return;
  anchorData.push_back({std::move(pendingAnchorId), page});
  pendingAnchorId.clear();
}

// Leaf storage services do not reenter semantics. Assembly operations install
// a state reborrow because they can query or notify Rust, including nested flushes.
const XpChapterServices ChapterPageAssembler::services_ = {
    [](void*, XpChapterState*, XpChapterSpan spelling) -> XpChapterSpan {
      const char* value = lookupHtmlEntity(reinterpret_cast<const char*>(spelling.data), spelling.len);
      return {reinterpret_cast<const uint8_t*>(value), value ? static_cast<uint32_t>(strlen(value)) : 0};
    },
    [](void* ctx, XpChapterState* state, uint32_t length) -> uint8_t* {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->entityBuffer(length);
    },
    nullptr,  // list_marker is owned by the Rust paragraph.
    nullptr,  // preserve_space is owned by the Rust paragraph.
    nullptr,  // paragraph_empty is owned by the Rust paragraph.
    nullptr,  // paragraph_size is owned by the Rust paragraph.
    nullptr,  // paragraph_scale is owned by the Rust paragraph.
    nullptr,  // paragraph_style is owned by the Rust paragraph.
    nullptr,  // suppress_indent is owned by the Rust paragraph.
    nullptr,  // ruby_capacity is owned by the Rust paragraph.
    nullptr,  // add_link is owned by the Rust paragraph.
    nullptr,  // link_matches is owned by the Rust paragraph.
    nullptr,  // size_slot is owned by the Rust paragraph.
    nullptr,  // set_paragraph_style is owned by the Rust paragraph.
    nullptr,  // word_style is owned by the Rust paragraph.
    nullptr,  // ruby_group is owned by the Rust paragraph.
    nullptr,  // ruby_append is owned by the Rust paragraph.
    nullptr,  // add_word is owned by the Rust paragraph.
    nullptr,  // paragraph_exists is owned by the Rust paragraph.
    [](void* ctx, XpChapterState* state, const XpChapterBlock* style) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->createParagraph(chapterBlock(*style));
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->resetParagraph();
    },
    [](void* ctx, XpChapterState* state) -> uint8_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->bodyIsSdFont();
    },
    [](void* ctx, XpChapterState* state) -> int32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->bodySpaceWidth();
    },
    [](void* ctx, XpChapterState* state, XpChapterSpan text, uint8_t style) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      char word[40];
      if (text.len >= sizeof(word)) {
        self->semanticFailLayout();
        return;
      }
      memcpy(word, text.data, text.len);
      word[text.len] = 0;
      self->prepareBodyText(word, style);
    },
    [](void* ctx, XpChapterState* state, XpChapterSpan text, uint8_t style) -> int32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      char word[40];
      if (text.len >= sizeof(word)) {
        self->semanticFailLayout();
        return 0;
      }
      memcpy(word, text.data, text.len);
      word[text.len] = 0;
      return self->bodyTextAdvance(word, static_cast<EpdFontFamily::Style>(style));
    },
    [](void* ctx, XpChapterState* state) -> int32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->bodyAscender();
    },
    [](void* ctx, XpChapterState* state) -> uint16_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->pageWidth();
    },
    [](void* ctx, XpChapterState* state) -> uint16_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->pageHeight();
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->clearTableCells();
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->reserveTableCells();
    },
    [](void* ctx, XpChapterState* state) -> uint32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->bufferedTableWords();
    },
    [](void* ctx, XpChapterState* state) -> uint8_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->tableStacked();
    },
    [](void* ctx, XpChapterState* state, uint8_t value) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->setTableStacked(value != 0);
    },
    [](void* ctx, XpChapterState* state, uint8_t value) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->setTableRtl(value != 0);
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->resetTableBytes();
    },
    [](void* ctx, XpChapterState* state) -> uint32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->tableBytes();
    },
    [](void* ctx, XpChapterState* state, uint32_t count) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->addTableBytes(count);
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->resetExtractedWords();
    },
    [](void* ctx, XpChapterState* state) -> int32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->extractedWords();
    },
    [](void* ctx, XpChapterState* state, int32_t index, const XpChapterFootnote* entry) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      FootnoteEntry value;
      memcpy(value.number, entry->number, sizeof(value.number));
      memcpy(value.href, entry->href, sizeof(value.href));
      self->addPendingFootnote(index, value);
    },
    [](void* ctx, XpChapterState* state, int32_t depth) -> uint8_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->boxAtDepth(depth);
    },
    [](void* ctx, XpChapterState* state, XpChapterSpan src, const uint8_t* css) -> uint8_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      return static_cast<uint8_t>(self->processImage(chapterString(src), chapterCss(css)));
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->measureTableAtCurrentPosition();
    },
    [](void* ctx, XpChapterState* state, const XpChapterBlock* style, uint8_t paragraph_end) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->startNewTextBlock(chapterBlock(*style), paragraph_end != 0);
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->flushPendingAnchor();
    },
    [](void* ctx, XpChapterState* state, XpChapterBlock* style, const uint8_t* css) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      auto value = chapterBlock(*style);
      self->openBoxScope(value, chapterCss(css));
      *style = chapterBlock(value);
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->closeBoxScope();
    },
    [](void* ctx, XpChapterState* state, const XpChapterBlock* style) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->breakTextLine(chapterBlock(*style));
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->fallbackTableRowToStacked();
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->closeTableCell();
    },
    [](void* ctx, XpChapterState* state) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->finishTableRow();
    },
    [](void* ctx, XpChapterState* state, uint8_t last_line, uint8_t paragraph_end) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->makePages(last_line != 0, paragraph_end != 0);
    },
    [](void* ctx, XpChapterState* state, const XpChapterBlock* style) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      SemanticBorrow borrow(*self, state);
      self->emitHorizontalRule(chapterBlock(*style));
    },
    [](void* ctx, XpChapterState* state, uint8_t defined, uint8_t rtl) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->applyParagraphDirection(defined != 0, rtl != 0);
    },
    [](void* ctx, XpChapterState* state, XpChapterSpan id) -> uint8_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      const std::string_view value(reinterpret_cast<const char*>(id.data), id.len);
      return std::any_of(self->tocAnchors.begin(), self->tocAnchors.end(),
                         [&](const auto& anchor) { return anchor == value; });
    },
    [](void* ctx, XpChapterState* state) -> uint32_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->anchorData.size();
    },
    [](void* ctx, XpChapterState* state) -> uint8_t {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      return self->semanticHasAnchor();
    },
    [](void* ctx, XpChapterState* state, XpChapterSpan id) -> void {
      auto* self = static_cast<ChapterPageAssembler*>(ctx);
      (void)state;
      self->pendingAnchorId.assign(reinterpret_cast<const char*>(id.data), id.len);
    },
    [](void* ctx, XpChapterState* state, const XpChapterTrace* record) -> void {
      (void)ctx;
      (void)state;
      (void)record;
    },
};
