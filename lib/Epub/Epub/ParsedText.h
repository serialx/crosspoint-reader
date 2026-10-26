#pragma once

#include <EpdFontFamily.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "WordStore.h"
#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

// Rust owns the cache allocation; element borrows expire on reserve/insert.
class ParagraphWidths {
  friend class ParsedText;
  XpLayoutWidths state{nullptr, 0, 0, paragraphAllocator(), 0};

 public:
  ParagraphWidths() = default;
  ~ParagraphWidths() { xp_layout_widths_destroy(&state); }
  ParagraphWidths(const ParagraphWidths&) = delete;
  ParagraphWidths& operator=(const ParagraphWidths&) = delete;
  ParagraphWidths(ParagraphWidths&& other) noexcept : state(other.state) {
    other.state.data = nullptr;
    other.state.len = other.state.capacity = 0;
  }
  void reserve(size_t n) { xp_layout_widths_reserve(&state, n); }
  void push_back(uint16_t width) { xp_layout_widths_insert(&state, state.len, width); }
  uint16_t& operator[](size_t i) { return state.data[i]; }
  uint16_t operator[](size_t i) const { return state.data[i]; }
  size_t size() const { return state.len; }
  bool valid() const { return state.failed == 0; }
};

class ParagraphBreaks {
  XpLayoutBreaks state{nullptr, 0, 0, paragraphAllocator(), 0};

 public:
  ParagraphBreaks() = default;
  ~ParagraphBreaks() { xp_layout_breaks_destroy(&state); }
  ParagraphBreaks(const ParagraphBreaks&) = delete;
  ParagraphBreaks& operator=(const ParagraphBreaks&) = delete;
  ParagraphBreaks(ParagraphBreaks&& other) noexcept : state(other.state) {
    other.state.data = nullptr;
    other.state.len = other.state.capacity = 0;
  }
  ParagraphBreaks& operator=(ParagraphBreaks&& other) noexcept {
    if (this != &other) {
      xp_layout_breaks_destroy(&state);
      state = other.state;
      other.state.data = nullptr;
      other.state.len = other.state.capacity = 0;
    }
    return *this;
  }
  bool push_back(size_t value) { return xp_layout_breaks_push(&state, value); }
  size_t operator[](size_t i) const { return state.data[i]; }
  size_t back() const { return state.data[state.len - 1]; }
  size_t size() const { return state.len; }
  bool empty() const { return state.len == 0; }
  void clear() { state.len = 0; }
};

class ParagraphLine {
  friend class ParsedText;
  XpLayoutLine state{nullptr, 0, paragraphAllocator(), 0};

 public:
  ParagraphLine(size_t count, bool focus) { xp_layout_line_init(&state, count, focus); }
  ~ParagraphLine() { xp_layout_line_destroy(&state); }
  ParagraphLine(const ParagraphLine&) = delete;
  ParagraphLine& operator=(const ParagraphLine&) = delete;
  bool valid() const { return state.data != nullptr; }
  std::span<TextBlock::SourceRange> sources() {
    static_assert(sizeof(TextBlock::SourceRange) == 8 && alignof(TextBlock::SourceRange) == 4);
    return {reinterpret_cast<TextBlock::SourceRange*>(state.data), state.count};
  }
  std::span<int16_t> positions() { return {reinterpret_cast<int16_t*>(state.data + 8 * state.count), state.count}; }
  std::span<EpdFontFamily::Style> styles() {
    static_assert(sizeof(EpdFontFamily::Style) == 1);
    return {reinterpret_cast<EpdFontFamily::Style*>(state.data + 10 * state.count), state.count};
  }
  std::span<uint8_t> boundaries() { return {state.data + 11 * state.count, state.focus ? state.count : 0}; }
  std::span<uint16_t> suffixes() {
    if (!state.focus) return {};
    return {reinterpret_cast<uint16_t*>(state.data + 12 * state.count), state.count};
  }
};

class GfxRenderer;

class ParsedText {
  WordStore wordStore;
  // Opaque Rust metadata state is embedded: no per-paragraph handle allocation.
  alignas(void*) mutable uint8_t tokenState[26 * sizeof(void*)];
  XpTokens* tokens() const { return reinterpret_cast<XpTokens*>(tokenState); }
  size_t tokenCount() const { return xp_tokens_size(tokens()); }
  XpToken tokenAt(size_t i) const {
    XpToken token;
    xp_tokens_get(tokens(), i, &token);
    return token;
  }
  WordStore::StoredWord storedWordAt(size_t i) const {
    const auto word = tokenAt(i).word;
    return {word.chunk, word.off, word.len};
  }
  EpdFontFamily::Style storedStyleAt(size_t i) const { return static_cast<EpdFontFamily::Style>(tokenAt(i).style); }
  bool tokenContinues(size_t i) const { return (tokenAt(i).flags & 1) != 0; }
  bool tokenNoSpaceBefore(size_t i) const { return (tokenAt(i).flags & 2) != 0; }
  static constexpr uint8_t MAX_SIZE_SLOTS = TextBlock::MAX_WORD_FONTS + 1;
  float sizeSlotScales[MAX_SIZE_SLOTS] = {1.0f};
  int32_t sizeSlotFontIds[MAX_SIZE_SLOTS] = {};
  uint8_t sizeSlotCount = 1;
  int wordFontId(size_t wordIndex, int blockFontId) const;
  std::vector<std::string> linkTargets;
  std::deque<std::string> rubyTexts;
  BlockStyle blockStyle;
  uint8_t wordSpacingPercent = 100;
  uint8_t paragraphIndentSpaces;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool isNaturalAlign;
  bool hasRtlWord;
  bool droppedWords = false;
  int linesAfterCurrent = -1;
  bool firstLineConsumed = false;
  // Separate from firstLineConsumed: a <br> line suppresses the indent but still starts a
  // new clipping paragraph.
  bool paragraphStartEmitted = false;
  std::vector<std::string> reorderedWordsScratch;
  std::vector<EpdFontFamily::Style> reorderedStylesScratch;
  std::vector<uint16_t> reorderedWidthsScratch;
  std::vector<bool> reorderedContinuesScratch;
  std::vector<bool> reorderedNoSpaceBeforeScratch;
  std::vector<uint8_t> reorderedFocusBoundaryScratch;
  std::vector<uint16_t> visualOrderScratch;

  std::string_view wordAt(const size_t i) const { return wordStore.view(storedWordAt(i)); }
  bool storeWord(std::string_view text, WordStore::StoredWord& out);
  uint32_t visibleOffsetAt(size_t wordIndex) const;
  uint32_t sourceOffsetAfter(uint32_t start, uint32_t renderedLength) const;
  bool recordAbsorbedSourceOffset(uint32_t offset);
  int calculateRubyExtraStartOffset(size_t wordIdx, size_t maxWordIdx, const GfxRenderer& renderer, int fontId) const;
  int calculateRubyExtraEndOffset(size_t lineStartIdx, size_t lineBreakIdx, const GfxRenderer& renderer,
                                  int fontId) const;
  int resolveFirstLineIndent(bool isFirstLine, const GfxRenderer& renderer, int fontId) const;
  ParagraphBreaks computeRustLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                        ParagraphWidths& wordWidths, size_t maxLines);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            ParagraphWidths& wordWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth, const ParagraphWidths& wordWidths,
                   const ParagraphBreaks& lineBreakIndices,
                   const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                   const GfxRenderer& renderer, int fontId);
  ParagraphWidths calculateWordWidths(const GfxRenderer& renderer, int fontId);

 public:
  explicit ParsedText(const bool hyphenationEnabled = false, const bool focusReadingEnabled = false,
                      const BlockStyle& blockStyle = BlockStyle(), const uint8_t paragraphIndentSpaces = 2)
      : blockStyle(blockStyle),
        paragraphIndentSpaces(paragraphIndentSpaces),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        isNaturalAlign(false),
        hasRtlWord(false) {
    xp_tokens_init(tokens(), paragraphAllocator());
  }
  ~ParsedText() { xp_tokens_destroy(tokens()); }
  ParsedText(const ParsedText&) = delete;
  ParsedText& operator=(const ParsedText&) = delete;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false,
               uint32_t visibleTextOffset = 0, uint8_t linkId = 0, uint8_t sizeSlot = 0, bool noSpaceBefore = false);
  uint8_t addLinkTarget(const char* href);
  bool linkTargetMatches(uint8_t linkId, const char* href) const;
  void setRubyForWordAt(size_t index, const std::string& ruby);
  void setRubyGroupAt(size_t startIndex, size_t count, const std::string& ruby);
  // The word's renderer style: stored flags removed, small caps restored from its size slot.
  EpdFontFamily::Style getWordStyleAt(size_t index) const {
    if (index >= tokenCount()) return EpdFontFamily::REGULAR;
    const auto token = tokenAt(index);
    const bool smallCaps = (token.slot & TextBlock::SMALL_CAPS_SLOT_FLAG) != 0;
    return static_cast<EpdFontFamily::Style>((token.style & ~TextBlock::DISCRETIONARY_HYPHEN_FLAG) |
                                             (smallCaps ? EpdFontFamily::SMALL_CAPS : 0));
  }
  std::string getRubyTextAt(size_t index) const { return index < rubyTexts.size() ? rubyTexts[index] : std::string(); }
  void ensureRubyCapacity();
  void suppressFirstLineIndent() { firstLineConsumed = true; }
  void resetFirstLineIndent() { firstLineConsumed = paragraphStartEmitted = false; }
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return tokenCount(); }
  bool isEmpty() const { return (tokenCount() == 0); }
  // True once any word was dropped because the text arena could not allocate.
  // Callers must treat the block as incomplete and fail the section build.
  bool hadDroppedWords() const { return droppedWords; }
  // While a line is being handed to layoutAndExtractLines' callback: how many lines of the
  // paragraph follow it, or -1 when the rest of the paragraph is not laid out yet.
  int linesAfterCurrentLine() const { return linesAfterCurrent; }
  // Slot for words at an absolute size; reuses a matching slot, or the nearest once all are taken.
  uint8_t sizeSlotFor(float scale);
  uint8_t sizeSlotsInUse() const { return sizeSlotCount; }
  float sizeSlotScale(uint8_t slot) const { return sizeSlotScales[slot]; }
  void setSizeSlotFontId(uint8_t slot, int32_t fontId) { sizeSlotFontIds[slot] = fontId; }
  // A set stop flag abandons extraction; the caller must discard this block.
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                             bool includeLastLine = true, int8_t characterSpacing = 0, uint8_t wordSpacingPercent = 100,
                             size_t maxLines = SIZE_MAX, const bool* stopRequested = nullptr);
};
