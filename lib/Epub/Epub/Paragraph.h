#pragma once

#include <EpdFontFamily.h>
#include <xp_ffi.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;
class Paragraph {
  alignas(void*) uint8_t state[64 * sizeof(void*)];

 public:
  XpParagraph* handle() { return reinterpret_cast<XpParagraph*>(state); }
  const XpParagraph* handle() const { return reinterpret_cast<const XpParagraph*>(state); }
  explicit Paragraph(bool hyphenation = false, bool focus = false, const BlockStyle& style = BlockStyle(),
                     uint8_t indent = 2);
  ~Paragraph() { xp_para_destroy(handle()); }
  Paragraph(const Paragraph&) = delete;
  Paragraph& operator=(const Paragraph&) = delete;
  void addWord(std::string_view word, EpdFontFamily::Style style, bool underline = false, bool attach = false,
               uint32_t offset = 0, uint8_t link = 0, uint8_t slot = 0, bool noSpace = false);
  uint8_t addLinkTarget(const char* href);
  bool linkTargetMatches(uint8_t link, const char* href) const;
  void setRubyForWordAt(size_t index, const std::string& text);
  void setRubyGroupAt(size_t index, size_t count, const std::string& text);
  std::string getRubyTextAt(size_t index) const;
  static void ensureRubyCapacity() {}
  EpdFontFamily::Style getWordStyleAt(size_t index) const {
    return static_cast<EpdFontFamily::Style>(xp_para_word_style(handle(), index));
  }
  void suppressFirstLineIndent() { xp_para_suppress_indent(handle()); }
  void resetFirstLineIndent() { xp_para_reset_indent(handle()); }
  BlockStyle getBlockStyle() const;
  void setBlockStyle(const BlockStyle& style);
  size_t size() const { return xp_para_size(handle()); }
  bool isEmpty() const { return size() == 0; }
  bool hadDroppedWords() const { return xp_para_dropped(handle()); }
  int linesAfterCurrentLine() const { return xp_para_lines_after(handle()); }
  uint8_t sizeSlotFor(float scale) { return xp_para_size_slot(handle(), scale); }
  uint8_t sizeSlotsInUse() const { return xp_para_slot_count(handle()); }
  float sizeSlotScale(uint8_t slot) const { return xp_para_slot_scale(handle(), slot); }
  void setSizeSlotFontId(uint8_t slot, int32_t font) { xp_para_slot_font(handle(), slot, font); }
  std::string_view wordAt(size_t index) const {
    const auto word = xp_para_word(handle(), index);
    return {reinterpret_cast<const char*>(word.data), word.len};
  }
  size_t tokenCount() const { return size(); }
  XpToken tokenAt(size_t index) const {
    XpToken token{};
    xp_para_token(handle(), index, &token);
    return token;
  }
  // Line spans are borrowed only for the callback. No TextBlock is allocated.
  void layoutLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth, void* context,
                   uint8_t (*processLine)(void*, const XpParaLine*), bool includeLastLine = true,
                   int8_t characterSpacing = 0, uint8_t wordSpacingPercent = 100, size_t maxLines = SIZE_MAX,
                   const bool* stopRequested = nullptr);
#ifdef XP_PAGE_CODEC_TEST
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                             bool includeLastLine = true, int8_t characterSpacing = 0, uint8_t wordSpacingPercent = 100,
                             size_t maxLines = SIZE_MAX, const bool* stopRequested = nullptr);
#endif
};
