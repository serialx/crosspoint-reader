#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>

#include <cstring>
#include <string_view>

#include "../../../../src/fontIds.h"
#include "../SectionCodec.h"

#ifdef XP_PAGE_CODEC_TEST
size_t TextBlock::arenaSize(const uint16_t wordCount, const bool hasFocus, const uint16_t textBytes) {
  // Layout documented in TextBlock.h: 16-bit arrays first, then 8-bit arrays, then text.
  size_t size =
      static_cast<size_t>(wordCount) * (sizeof(SourceRange) + sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasFocus) {
    size += static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(uint8_t));
  }
  return size + textBytes;
}

#endif

void TextBlock::bindArenaPointers(const uint8_t* base) {
  const size_t wc = numWords;
  sourceRanges = reinterpret_cast<const SourceRange*>(base);
  base += wc * sizeof(SourceRange);
  textOffArr = reinterpret_cast<const uint16_t*>(base);
  xposArr = reinterpret_cast<const int16_t*>(base + wc * 2);
  size_t off = wc * 4;
  if (focusPresent) {
    focusSuffixXArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  stylesArr = base + off;
  off += wc;
  if (focusPresent) {
    focusBoundaryArr = base + off;
    off += wc;
  }
  textArr = reinterpret_cast<const char*>(base + off);
}

#ifdef XP_PAGE_CODEC_TEST
bool TextBlock::initializeLayout(const size_t count, std::span<const int16_t> wordXpos,
                                 std::span<const EpdFontFamily::Style> wordStyles,
                                 std::span<const uint8_t> focusBoundary, std::span<const uint16_t> focusSuffixX,
                                 std::span<const SourceRange> ranges) {
  if (!hasRuby()) this->rubyTexts = std::vector<std::string>{};
  // Focus annotations are optional: empty vectors mean no word in this block has a split.
  // When present, they must be sized in lockstep with words[].
  const bool hasFocus = !focusBoundary.empty();
  if ((!ranges.empty() && ranges.size() != count) || count != wordXpos.size() || count != wordStyles.size() ||
      count > 10000 || (paragraphStartWord != UINT16_MAX && paragraphStartWord >= count) ||
      (hasFocus && (count != focusBoundary.size() || count != focusSuffixX.size()))) {
    LOG_ERR("TXB", "Construction failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u)",
            static_cast<uint32_t>(count), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(focusBoundary.size()),
            static_cast<uint32_t>(focusSuffixX.size()));
    isValid = false;
    return false;
  }

  numWords = static_cast<uint16_t>(count);
  focusPresent = hasFocus;
  if (numWords == 0) {
    return true;  // valid empty block, no arena
  }

  return true;
}

bool TextBlock::initializeArena(const size_t totalText) {
  if (totalText > UINT16_MAX) {
    LOG_ERR("TXB", "Construction failed: text size %u exceeds arena limit", static_cast<uint32_t>(totalText));
    numWords = 0;
    focusPresent = false;
    isValid = false;
    return false;
  }
  textBytes = static_cast<uint16_t>(totalText);

  const size_t size = arenaSize(numWords, focusPresent, textBytes);
  arena = makeUniqueNoThrow<uint8_t[]>(size);
  if (!arena) {
    // Evict rebuildable caches (SD-font mini data, render glyph cache) and
    // retry once before declaring the line lost.
    freeink::MemoryManager::instance().ensureFree(size + 4 * 1024);
    arena = makeUniqueNoThrow<uint8_t[]>(size);
  }
  if (!arena) {
    LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
    numWords = 0;
    textBytes = 0;
    focusPresent = false;
    isValid = false;
    return false;
  }
  bindArenaPointers(arena.get());

  return true;
}

void TextBlock::copyLayout(std::span<const int16_t> wordXpos, std::span<const EpdFontFamily::Style> wordStyles,
                           std::span<const uint8_t> focusBoundary, std::span<const uint16_t> focusSuffixX,
                           std::span<const SourceRange> ranges) {
  for (uint16_t i = 0; i < numWords; ++i) {
    const_cast<SourceRange*>(sourceRanges)[i] = ranges.empty() ? SourceRange{} : ranges[i];
    const_cast<int16_t*>(xposArr)[i] = wordXpos[i];
    const_cast<uint8_t*>(stylesArr)[i] = static_cast<uint8_t>(wordStyles[i]);
    if (focusPresent) {
      const_cast<uint16_t*>(focusSuffixXArr)[i] = focusSuffixX[i];
      const_cast<uint8_t*>(focusBoundaryArr)[i] = focusBoundary[i];
    }
  }
}

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const BlockStyle& blockStyle,
                     std::vector<std::string> rubyTexts, std::vector<LinkSpan> linkSpans,
                     const std::vector<SourceRange>& ranges, const uint16_t paragraphStartWord)
    : TextBlock(BorrowedLayout{}, words, wordXpos, wordStyles, focusBoundary, focusSuffixX, blockStyle,
                std::move(rubyTexts), std::move(linkSpans), ranges, paragraphStartWord) {}

TextBlock::TextBlock(BorrowedLayout, const std::vector<std::string>& words, std::span<const int16_t> wordXpos,
                     std::span<const EpdFontFamily::Style> wordStyles, std::span<const uint8_t> focusBoundary,
                     std::span<const uint16_t> focusSuffixX, const BlockStyle& blockStyle,
                     std::vector<std::string> rubyTexts, std::vector<LinkSpan> linkSpans,
                     std::span<const SourceRange> ranges, const uint16_t paragraphStartWord)
    : blockStyle(blockStyle), paragraphStartWord(paragraphStartWord), rubyTexts(std::move(rubyTexts)) {
  if (!allocateLinks(linkSpans.size())) return;
  for (size_t i = 0; i < linkSpans.size(); ++i) this->linkSpans.data[i] = linkSpans[i];
  if (!initializeLayout(words.size(), wordXpos, wordStyles, focusBoundary, focusSuffixX, ranges) || numWords == 0)
    return;
  size_t totalText = 0;
  for (size_t i = 0; i < numWords; ++i) totalText += words[i].size() + 1;
  if (!initializeArena(totalText)) return;
  copyLayout(wordXpos, wordStyles, focusBoundary, focusSuffixX, ranges);
  auto* textOff = const_cast<uint16_t*>(textOffArr);
  auto* text = const_cast<char*>(textArr);
  uint16_t off = 0;
  for (uint16_t i = 0; i < numWords; ++i) {
    textOff[i] = off;
    memcpy(text + off, words[i].data(), words[i].size());
    off += static_cast<uint16_t>(words[i].size());
    text[off++] = '\0';
  }
}

TextBlock::TextBlock(BorrowedLayout, std::span<const char> packedText, std::span<const uint16_t> offsets,
                     std::span<const int16_t> wordXpos, std::span<const EpdFontFamily::Style> wordStyles,
                     std::span<const uint8_t> focusBoundary, std::span<const uint16_t> focusSuffixX,
                     const BlockStyle& blockStyle, std::vector<std::string> rubyTexts,
                     std::span<const XpParaLink> linkSpans, std::span<const SourceRange> ranges,
                     const uint16_t paragraphStartWord)
    : blockStyle(blockStyle), paragraphStartWord(paragraphStartWord), rubyTexts(std::move(rubyTexts)) {
  if (!allocateLinks(linkSpans.size())) return;
  for (size_t i = 0; i < linkSpans.size(); ++i) {
    auto& link = this->linkSpans.data[i];
    memcpy(link.href, linkSpans[i].href, sizeof(link.href));
    link.x = linkSpans[i].x;
    link.width = linkSpans[i].width;
    link.topLift = linkSpans[i].top_lift;
  }
  for (const auto offset : offsets) {
    if (offset >= packedText.size() || !memchr(packedText.data() + offset, 0, packedText.size() - offset)) {
      LOG_ERR("TXB", "Construction failed: invalid packed word");
      isValid = false;
      return;
    }
  }
  const auto wordAt = [&](size_t i) { return std::string_view(packedText.data() + offsets[i]); };
  if (!initializeLayout(offsets.size(), wordXpos, wordStyles, focusBoundary, focusSuffixX, ranges) || numWords == 0)
    return;
  size_t totalText = 0;
  for (size_t i = 0; i < numWords; ++i) totalText += wordAt(i).size() + 1;
  if (!initializeArena(totalText)) return;
  copyLayout(wordXpos, wordStyles, focusBoundary, focusSuffixX, ranges);
  auto* textOff = const_cast<uint16_t*>(textOffArr);
  auto* text = const_cast<char*>(textArr);
  uint16_t off = 0;
  for (uint16_t i = 0; i < numWords; ++i) {
    textOff[i] = off;
    memcpy(text + off, wordAt(i).data(), wordAt(i).size());
    off += static_cast<uint16_t>(wordAt(i).size());
    text[off++] = '\0';
  }
}

bool TextBlock::allocateLinks(const size_t count) {
  if (count == 0) return true;
  linkSpans.data = makeUniqueNoThrow<LinkSpan[]>(count);
  if (!linkSpans.data) {
    LOG_ERR("TXB", "OOM: links %u bytes", static_cast<uint32_t>(count * sizeof(LinkSpan)));
    isValid = false;
    return false;
  }
  linkSpans.count = count;
  return true;
}

#endif

bool TextBlock::hasRuby() const {
  if (pageArena) return pageHasRuby;
#ifdef XP_PAGE_CODEC_TEST
  if (borrowedRuby) return !borrowedRuby->empty();
  for (const auto& rt : rubyTexts) {
    if (!rt.empty()) return true;
  }
#endif
  return false;
}

#ifdef XP_PAGE_CODEC_TEST
bool TextBlock::setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]) {
  wordFontData = makeUniqueNoThrow<uint8_t[]>(sizeof(fonts) + numWords);
  fontData = wordFontData.get();
  if (!wordFontData) return false;
  memcpy(wordFontData.get(), fonts, sizeof(fonts));
  memcpy(wordFontData.get() + sizeof(fonts), slots, numWords);
  return true;
}

#endif

int TextBlock::renderFontId(const GfxRenderer& renderer, const int fontId, const int sectionFontId) {
  return fontId == sectionFontId || renderer.ensureFontLoaded(fontId) ? fontId : sectionFontId;
}

int TextBlock::wordFontId(const uint16_t i, const int sectionFontId) const {
  if (!fontData) return blockFontId(sectionFontId);
  const uint8_t slot = fontData[sizeof(int32_t) * MAX_WORD_FONTS + i] & ~SMALL_CAPS_SLOT_FLAG;
  if (slot == 0) return blockFontId(sectionFontId);
  int32_t font = 0;
  memcpy(&font, fontData + sizeof(int32_t) * (slot - 1), sizeof(font));
  return font;
}

int TextBlock::lineAscent(const GfxRenderer& renderer, const int sectionFontId) const {
  int ascent = renderer.getFontAscenderSize(renderFontId(renderer, blockFontId(sectionFontId), sectionFontId));
  if (!fontData) return ascent;
  for (uint8_t slot = 0; slot < MAX_WORD_FONTS; ++slot) {
    int32_t font = 0;
    memcpy(&font, fontData + sizeof(int32_t) * slot, sizeof(font));
    if (font != 0) ascent = std::max(ascent, renderer.getFontAscenderSize(renderFontId(renderer, font, sectionFontId)));
  }
  return ascent;
}

int TextBlock::wordYOffset(const GfxRenderer& renderer, const int sectionFontId, const uint16_t i) const {
  if (!fontData) return 0;
  return lineAscent(renderer, sectionFontId) -
         renderer.getFontAscenderSize(renderFontId(renderer, wordFontId(i, sectionFontId), sectionFontId));
}

int TextBlock::extraAscent(const GfxRenderer& renderer, const int sectionFontId) const {
  if (!fontData) return 0;
  return lineAscent(renderer, sectionFontId) -
         renderer.getFontAscenderSize(renderFontId(renderer, blockFontId(sectionFontId), sectionFontId));
}

#ifdef XP_PAGE_CODEC_TEST
TextBlock::TextBlock(View, const TextBlock& s)
    : blockStyle(s.getBlockStyle()),
      numWords(s.numWords),
      paragraphStartWord(s.paragraphStartWord),
      textBytes(s.textBytes),
      focusPresent(s.focusPresent),
      isValid(s.isValid),
      sourceRanges(s.sourceRanges),
      textOffArr(s.textOffArr),
      xposArr(s.xposArr),
      focusSuffixXArr(s.focusSuffixXArr),
      stylesArr(s.stylesArr),
      focusBoundaryArr(s.focusBoundaryArr),
      textArr(s.textArr),
      pageArena(s.pageArena),
      rubyOffsets(s.rubyOffsets),
      pageHasRuby(s.pageHasRuby),
      fontData(s.fontData),
      borrowedRuby(s.borrowedRuby ? s.borrowedRuby : &s.rubyTexts) {}

#endif

TextBlock::TextBlock(View, const uint8_t* page, const size_t size, const uint16_t element) : pageArena(page) {
  XpPageLine line{};
  if (xp_page_line(page, size, element, &line)) {
    isValid = false;
    return;
  }
  numWords = line.count;
  paragraphStartWord = line.paragraph_start;
  focusPresent = line.has_focus;
  pageHasRuby = line.has_ruby;
  rubyOffsets = line.ruby_offsets.data;
  textBytes = line.packed.len - size_t(numWords) * (13 + (focusPresent ? 3 : 0));
  if (numWords) bindArenaPointers(line.packed.data);
  fontData = line.font_data.len ? line.font_data.data : nullptr;
  const auto& style = line.block;
  blockStyle.alignment = static_cast<CssTextAlign>(style.values[0]);
  blockStyle.textAlignDefined = style.values[1];
  blockStyle.marginTop = style.values[2];
  blockStyle.marginBottom = style.values[3];
  blockStyle.marginLeft = style.values[4];
  blockStyle.marginRight = style.values[5];
  blockStyle.paddingTop = style.values[6];
  blockStyle.paddingBottom = style.values[7];
  blockStyle.paddingLeft = style.values[8];
  blockStyle.paddingRight = style.values[9];
  blockStyle.textIndent = style.values[10];
  blockStyle.textIndentDefined = style.values[11];
  blockStyle.isRtl = style.values[12];
  blockStyle.directionDefined = style.values[13];
  blockStyle.characterSpacing = style.values[14];
  blockStyle.fontId = style.values[15];
}

bool TextBlock::resolveView(const uint8_t* page, const size_t size, const uint16_t element, ViewData& out) {
  XpPageLine line{};
  if (xp_page_line(page, size, element, &line)) return false;
  out = {line.packed.data,
         line.style_data.data,
         line.font_data.len ? line.font_data.data : nullptr,
         line.ruby_offsets.data,
         line.count,
         line.paragraph_start,
         static_cast<uint16_t>(line.packed.len - size_t(line.count) * (13 + (line.has_focus ? 3 : 0))),
         line.has_focus != 0,
         line.has_ruby != 0};
  return true;
}

TextBlock::TextBlock(View, const uint8_t* page, const ViewData& data)
    : numWords(data.count),
      paragraphStartWord(data.paragraphStart),
      textBytes(data.textBytes),
      focusPresent(data.focus),
      pageArena(page),
      rubyOffsets(data.rubies),
      pageHasRuby(data.ruby),
      fontData(data.fonts) {
  if (numWords) bindArenaPointers(data.packed);
  const auto* p = data.style;
  const auto read16 = [](const uint8_t* p) {
    int16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
  };
  blockStyle.alignment = static_cast<CssTextAlign>(p[0]);
  blockStyle.textAlignDefined = p[1];
  blockStyle.marginTop = read16(p + 2);
  blockStyle.marginBottom = read16(p + 4);
  blockStyle.marginLeft = read16(p + 6);
  blockStyle.marginRight = read16(p + 8);
  blockStyle.paddingTop = read16(p + 10);
  blockStyle.paddingBottom = read16(p + 12);
  blockStyle.paddingLeft = read16(p + 14);
  blockStyle.paddingRight = read16(p + 16);
  blockStyle.textIndent = read16(p + 18);
  blockStyle.textIndentDefined = p[20];
  blockStyle.isRtl = p[21];
  blockStyle.directionDefined = p[22];
  blockStyle.characterSpacing = static_cast<int8_t>(p[23]);
  memcpy(&blockStyle.fontId, p + 24, sizeof(blockStyle.fontId));
}

std::string_view TextBlock::pageRubyText(const uint16_t i) const {
  if (i >= numWords) return "";
  const uint32_t offset = unaligned(reinterpret_cast<const uint32_t*>(rubyOffsets + size_t(i) * 4));
  const uint8_t* record = pageArena + offset;
  const uint32_t length = unaligned(reinterpret_cast<const uint32_t*>(record));
  return {reinterpret_cast<const char*>(record + 4), length};
}
