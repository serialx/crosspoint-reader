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

// Borrowed line view into a decoded Page. Every word is NUL-terminated;
// multi-byte fields use memcpy because wire records need not be aligned.
// Legacy owning constructors exist only for host codec/layout fixtures.
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
#ifdef XP_PAGE_CODEC_TEST
  // Owning storage exists only in legacy codec/layout fixtures.
  std::unique_ptr<uint8_t[]> arena;
#endif
  // Typed views into packed data. Cached wire records can be unaligned;
  // multi-byte accessors therefore use memcpy.
  const SourceRange* sourceRanges = nullptr;
  const uint16_t* textOffArr = nullptr;
  const int16_t* xposArr = nullptr;
  const uint16_t* focusSuffixXArr = nullptr;  // null when !focusPresent
  const uint8_t* stylesArr = nullptr;
  const uint8_t* focusBoundaryArr = nullptr;  // null when !focusPresent
  const char* textArr = nullptr;
#ifdef XP_PAGE_CODEC_TEST
  std::vector<std::string> rubyTexts;
#endif
#ifdef XP_PAGE_CODEC_TEST
  std::unique_ptr<uint8_t[]> wordFontData;
#endif
#ifdef XP_PAGE_CODEC_TEST
  LinkSpans linkSpans;
#endif

  const uint8_t* pageArena = nullptr;
  const uint8_t* rubyOffsets = nullptr;
  bool pageHasRuby = false;
  const uint8_t* fontData = nullptr;
#ifdef XP_PAGE_CODEC_TEST
  const std::vector<std::string>* borrowedRuby = nullptr;
#endif
  std::string_view pageRubyText(uint16_t i) const;

  template <typename T>
  static T unaligned(const T* ptr) {
    T value;
    memcpy(&value, ptr, sizeof(value));
    return value;
  }
  uint16_t textOffset(uint16_t i) const { return unaligned(textOffArr + i); }
  TextBlock() = default;
#ifdef XP_PAGE_CODEC_TEST
  static size_t arenaSize(uint16_t wordCount, bool hasFocus, uint16_t textBytes);
#endif
  void bindArenaPointers(const uint8_t* base);
#ifdef XP_PAGE_CODEC_TEST
  bool initializeLayout(size_t count, std::span<const int16_t> xpos, std::span<const EpdFontFamily::Style> styles,
                        std::span<const uint8_t> boundary, std::span<const uint16_t> suffix,
                        std::span<const SourceRange> ranges);
  bool initializeArena(size_t totalText);
  bool allocateLinks(size_t count);
  void copyLayout(std::span<const int16_t> xpos, std::span<const EpdFontFamily::Style> styles,
                  std::span<const uint8_t> boundary, std::span<const uint16_t> suffix,
                  std::span<const SourceRange> ranges);
#endif

 public:
#ifdef XP_PAGE_CODEC_TEST
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
#endif

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
#ifdef XP_PAGE_CODEC_TEST
  TextBlock(View, const TextBlock& source);
#endif
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
#ifdef XP_PAGE_CODEC_TEST
  bool setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]);
#endif
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
#ifdef XP_PAGE_CODEC_TEST
    if (!pageArena) return borrowedRuby ? borrowedRuby->size() : rubyTexts.size();
#endif
    return pageHasRuby ? numWords : 0;
  }
  std::string_view rubyText(uint16_t i) const {
    if (pageArena) return pageHasRuby ? pageRubyText(i) : std::string_view("");
#ifdef XP_PAGE_CODEC_TEST
    const auto& ruby = borrowedRuby ? *borrowedRuby : rubyTexts;
    return i < ruby.size() ? std::string_view(ruby[i]) : std::string_view("");
#else
    return "";
#endif
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
#ifdef XP_PAGE_CODEC_TEST
  LinkSpans takeLinkSpans() { return std::exchange(linkSpans, LinkSpans{}); }
#endif

  BlockType getType() override { return TEXT_BLOCK; }
#ifdef XP_PAGE_CODEC_TEST
  bool serialize(HalFile& file) const;
#endif
};
