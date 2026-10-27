#pragma once

#include <EpdFontFamily.h>
#include <HalStorage.h>
#include <xp_ffi.h>

#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Block.h"
#include "BlockStyle.h"
#include "Epub/FootnoteEntry.h"

// Represents an owned layout line or a borrowed line in a decoded Page.
// Layout construction uses one allocation for packed word data; loaded views
// borrow the page arena and allocate nothing.
//
// Arena layout, in order (4-byte alignment holds by construction: source ranges come first, then 16-bit
// arrays and the arena base is allocator-aligned; RISC-V faults on
// unaligned multi-byte access):
//   SourceRange sourceRanges[wordCount]  chapter codepoint offsets, [start, end)
//   uint16_t textOff[wordCount]        byte offset of word i's text in text[]
//   int16_t  xpos[wordCount]
//   uint16_t focusSuffixX[wordCount]   present only when focusPresent
//   uint8_t  styles[wordCount]
//   uint8_t  focusBoundary[wordCount]  present only when focusPresent
//   char     text[textBytes]           all words back to back, NUL-terminated
//
// Each word is stored NUL-terminated so render() can hand `text + textOff[i]`
// straight to C APIs (drawText) with no std::string materialization.
//
// Focus split semantics (unchanged from the vector layout): boundary N > 0
// means the first N bytes of word i render bold, the remainder in the base
// style. N is bounded to 9 codepoints (<= 36 UTF-8 bytes) by the clamp in
// Paragraph::addWord. focusSuffixX is the pre-computed pixel offset from the
// word start to the regular suffix. Both arrays are omitted from the arena
// entirely when no word on the line has a split (zero per-word RAM cost when
// focus reading is disabled).
class TextBlock final : public Block {
 public:
  // The unused high bit of the cached style byte carries clipping metadata.
  static constexpr uint8_t DISCRETIONARY_HYPHEN_FLAG = 0x80;
  // Word font slot bit for CSS small caps; the style byte's high bit is the hyphen flag.
  static constexpr uint8_t SMALL_CAPS_SLOT_FLAG = 0x80;
  struct SourceRange {
    uint32_t start = UINT32_MAX;
    uint32_t end = UINT32_MAX;
  };

  struct LinkSpan {
    char href[FOOTNOTE_HREF_LEN];
    int16_t x;
    int16_t width;
    int16_t topLift;
  };
  struct LinkSpans {
    std::unique_ptr<LinkSpan[]> data;
    size_t count = 0;
    const LinkSpan* begin() const { return data.get(); }
    const LinkSpan* end() const { return count ? data.get() + count : data.get(); }
  };

 private:
  BlockStyle blockStyle;
  uint16_t numWords = 0;
  uint16_t paragraphStartWord = UINT16_MAX;  // Visual index of the paragraph's first logical word.
  uint16_t textBytes = 0;                    // total size of the text region, including NULs
  bool focusPresent = false;
  bool isValid = true;
  // Per-word metadata and text share one checked allocation.
  std::unique_ptr<uint8_t[]> arena;
  // Typed views into packed data. Cached wire records can be unaligned;
  // multi-byte accessors therefore use memcpy.
  const SourceRange* sourceRanges = nullptr;
  const uint16_t* textOffArr = nullptr;
  const int16_t* xposArr = nullptr;
  const uint16_t* focusSuffixXArr = nullptr;  // null when !focusPresent
  const uint8_t* stylesArr = nullptr;
  const uint8_t* focusBoundaryArr = nullptr;  // null when !focusPresent
  const char* textArr = nullptr;
  std::vector<std::string> rubyTexts;
  // Words sized apart from the line (CSS font-size spans): MAX_WORD_FONTS int32 font ids,
  // then one slot per word (0 = block font, n = font n-1). Null when every word uses the
  // block font, so ordinary lines pay only this pointer.
  std::unique_ptr<uint8_t[]> wordFontData;
  // Layout-only metadata. ChapterHtmlSlimParser moves it into Page::links
  // immediately; cached TextBlocks therefore keep the same compact format.
  LinkSpans linkSpans;

  const uint8_t* pageArena = nullptr;
  const uint8_t* rubyOffsets = nullptr;
  bool pageHasRuby = false;
  const uint8_t* fontData = nullptr;
  const std::vector<std::string>* borrowedRuby = nullptr;
  std::string_view pageRubyText(uint16_t i) const;

  template <typename T>
  static T unaligned(const T* ptr) {
    T value;
    memcpy(&value, ptr, sizeof(value));
    return value;
  }
  uint16_t textOffset(uint16_t i) const { return unaligned(textOffArr + i); }
  TextBlock() = default;
  static size_t arenaSize(uint16_t wordCount, bool hasFocus, uint16_t textBytes);
  void bindArenaPointers(const uint8_t* base);
  bool initializeLayout(size_t count, std::span<const int16_t> xpos, std::span<const EpdFontFamily::Style> styles,
                        std::span<const uint8_t> boundary, std::span<const uint16_t> suffix,
                        std::span<const SourceRange> ranges);
  bool initializeArena(size_t totalText);
  bool allocateLinks(size_t count);
  void copyLayout(std::span<const int16_t> xpos, std::span<const EpdFontFamily::Style> styles,
                  std::span<const uint8_t> boundary, std::span<const uint16_t> suffix,
                  std::span<const SourceRange> ranges);

 public:
  // Flatten-on-construct: copies the layout-time vectors into the arena; the
  // vectors die with the caller. On arena OOM the block is empty and valid()
  // is false -- callers must check and fail the line instead of using it.
  explicit TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const BlockStyle& blockStyle = BlockStyle(),
                     std::vector<std::string> rubyTexts = {}, std::vector<LinkSpan> linkSpans = {},
                     const std::vector<SourceRange>& ranges = {}, uint16_t paragraphStartWord = UINT16_MAX);
  struct BorrowedLayout {};
  TextBlock(BorrowedLayout, const std::vector<std::string>& words, std::span<const int16_t> wordXpos,
            std::span<const EpdFontFamily::Style> wordStyles, std::span<const uint8_t> focusBoundary,
            std::span<const uint16_t> focusSuffixX, const BlockStyle& blockStyle = BlockStyle(),
            std::vector<std::string> rubyTexts = {}, std::vector<LinkSpan> linkSpans = {},
            std::span<const SourceRange> ranges = {}, uint16_t paragraphStartWord = UINT16_MAX);
  TextBlock(BorrowedLayout, std::span<const char> text, std::span<const uint16_t> offsets,
            std::span<const int16_t> wordXpos, std::span<const EpdFontFamily::Style> wordStyles,
            std::span<const uint8_t> focusBoundary, std::span<const uint16_t> focusSuffixX,
            const BlockStyle& blockStyle = BlockStyle(), std::vector<std::string> rubyTexts = {},
            std::span<const XpParaLink> linkSpans = {}, std::span<const SourceRange> ranges = {},
            uint16_t paragraphStartWord = UINT16_MAX);
  // These views borrow their source until the page/owned block is destroyed.
  struct View {};
  // Resolved once at page load and stored in the page's single allocation.
  struct ViewData {
    const uint8_t* packed;
    const uint8_t* style;
    const uint8_t* fonts;
    const uint8_t* rubies;
    uint16_t count, paragraphStart, textBytes;
    bool focus, ruby;
  };
  static bool resolveView(const uint8_t* page, size_t size, uint16_t element, ViewData& out);
  TextBlock(View, const uint8_t* page, const ViewData& data);
  TextBlock(View, const uint8_t* page, size_t size, uint16_t element);
  TextBlock(View, const TextBlock& source);
  ~TextBlock() override = default;
  TextBlock(const TextBlock&) = delete;
  TextBlock& operator=(const TextBlock&) = delete;

  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  // The font this line was laid out in: a CSS font-size override, else the section font.
  int blockFontId(const int sectionFontId) const {
    return getBlockStyle().fontId ? getBlockStyle().fontId : sectionFontId;
  }

  static constexpr uint8_t MAX_WORD_FONTS = 3;
  // slots[i] picks word i's font: 0 = the block font, n = fonts[n-1]; SMALL_CAPS_SLOT_FLAG
  // marks small caps. Returns false on OOM.
  bool setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]);
  int wordFontId(uint16_t i, int sectionFontId) const;
  bool wordHasSmallCaps(uint16_t i) const {
    return fontData && (fontData[sizeof(int32_t) * MAX_WORD_FONTS + i] & SMALL_CAPS_SLOT_FLAG);
  }
  // fontId when it is (or can be) registered with the renderer, else the section font.
  static int renderFontId(const GfxRenderer& renderer, int fontId, int sectionFontId);
  // Ascent of the line's shared baseline: the tallest font among its words.
  int lineAscent(const GfxRenderer& renderer, int sectionFontId) const;
  // Offset from the line top at which to draw word i so every word shares the baseline.
  int wordYOffset(const GfxRenderer& renderer, int sectionFontId, uint16_t i) const;
  // Extra height the line needs beyond its block font for taller words.
  int extraAscent(const GfxRenderer& renderer, int sectionFontId) const;
  bool isEmpty() override { return numWords == 0; }
  bool valid() const { return isValid; }
  uint16_t wordCount() const { return numWords; }
  // NUL-terminated by construction; safe to pass to C APIs directly.
  const char* wordText(const uint16_t i) const { return textArr + textOffset(i); }
  uint16_t wordTextLen(const uint16_t i) const {
    const uint16_t end = (i + 1 < numWords) ? textOffset(i + 1) : textBytes;
    return end - textOffset(i) - 1;  // exclude the NUL
  }
  SourceRange wordSourceRange(const uint16_t i) const { return unaligned(sourceRanges + i); }
  bool wordStartsParagraph(const uint16_t i) const { return i == paragraphStartWord; }
  int16_t wordXpos(const uint16_t i) const { return unaligned(xposArr + i); }
  EpdFontFamily::Style wordStyle(const uint16_t i) const {
    return static_cast<EpdFontFamily::Style>((stylesArr[i] & ~DISCRETIONARY_HYPHEN_FLAG) |
                                             (wordHasSmallCaps(i) ? EpdFontFamily::SMALL_CAPS : 0));
  }
  bool wordHasDiscretionaryHyphen(const uint16_t i) const { return (stylesArr[i] & DISCRETIONARY_HYPHEN_FLAG) != 0; }
  uint8_t focusBoundary(const uint16_t i) const { return focusPresent ? focusBoundaryArr[i] : 0; }
  uint16_t focusSuffixX(const uint16_t i) const { return focusPresent ? unaligned(focusSuffixXArr + i) : 0; }
  bool hasRuby() const;
  int getRubyShift(int ascender) const { return hasRuby() ? (ascender / 2) : 0; }
  size_t rubyCount() const {
    return pageArena ? (pageHasRuby ? numWords : 0) : (borrowedRuby ? borrowedRuby->size() : rubyTexts.size());
  }
  std::string_view rubyText(uint16_t i) const {
    if (pageArena) return pageHasRuby ? pageRubyText(i) : std::string_view("");
    const auto& ruby = borrowedRuby ? *borrowedRuby : rubyTexts;
    return i < ruby.size() ? std::string_view(ruby[i]) : std::string_view("");
  }
  class RubyTexts {
    const TextBlock* block;

   public:
    explicit RubyTexts(const TextBlock* block) : block(block) {}
    size_t size() const { return block->rubyCount(); }
    bool empty() const { return size() == 0; }
    std::string_view operator[](uint16_t i) const { return block->rubyText(i); }
    std::string_view back() const { return (*this)[size() - 1]; }
    struct Iterator {
      const TextBlock* block;
      uint16_t index;
      std::string_view operator*() const { return block->rubyText(index); }
      Iterator& operator++() {
        ++index;
        return *this;
      }
      bool operator!=(const Iterator& other) const { return index != other.index; }
    };
    Iterator begin() const { return {block, 0}; }
    Iterator end() const { return {block, static_cast<uint16_t>(size())}; }
  };
  RubyTexts getRubyTexts() const { return RubyTexts(this); }
  LinkSpans takeLinkSpans() { return std::exchange(linkSpans, LinkSpans{}); }

  void render(const GfxRenderer& renderer, int fontId, int x, int y) const;
  BlockType getType() override { return TEXT_BLOCK; }
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) const;
#endif
};
