#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <MemoryManager.h>

#include <cstring>
#include <string_view>

#include "../../../../src/fontIds.h"
#include "../CjkBreaks.h"
#include "../SectionCodec.h"

size_t TextBlock::arenaSize(const uint16_t wordCount, const bool hasFocus, const uint16_t textBytes) {
  // Layout documented in TextBlock.h: 16-bit arrays first, then 8-bit arrays, then text.
  size_t size =
      static_cast<size_t>(wordCount) * (sizeof(SourceRange) + sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasFocus) {
    size += static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(uint8_t));
  }
  return size + textBytes;
}

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

bool TextBlock::hasRuby() const {
  if (pageArena) return pageHasRuby;
  if (borrowedRuby) return !borrowedRuby->empty();
  for (const auto& rt : rubyTexts) {
    if (!rt.empty()) return true;
  }
  return false;
}

bool TextBlock::setWordFonts(const uint8_t* slots, const int32_t (&fonts)[MAX_WORD_FONTS]) {
  wordFontData = makeUniqueNoThrow<uint8_t[]>(sizeof(fonts) + numWords);
  fontData = wordFontData.get();
  if (!wordFontData) return false;
  memcpy(wordFontData.get(), fonts, sizeof(fonts));
  memcpy(wordFontData.get() + sizeof(fonts), slots, numWords);
  return true;
}

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

void TextBlock::render(const GfxRenderer& renderer, const int sectionFontId, const int x, const int y) const {
  if (!isValid) {
    LOG_ERR("TXB", "Render skipped: invalid block");
    return;
  }
  const auto& blockStyle = getBlockStyle();
  // A sized variant that can no longer be loaded falls back to the section font.
  const int fontId = renderFontId(renderer, blockFontId(sectionFontId), sectionFontId);
  const int8_t tracking = blockStyle.characterSpacing;

  const bool scanning = renderer.isFontCacheScanning();
  // Words in other sizes share the baseline set by the line's tallest font.
  const int ascender = lineAscent(renderer, sectionFontId);

  // Resolve ruby positions. Layout (extractLine) has already reserved extraStartOffset on the
  // left and extraEndOffset on the right, so the centered rubyX is always within the page margins.
  struct RubyDrawInfo {
    int x = 0;  // spread == 0: centered x. spread > 0: left edge of the base text.
    std::string_view text;
    BidiUtils::BidiBaseDir baseDir{};
    // ruby-align: space-around. >0 when the ruby is narrower than its base: the width difference
    // to distribute over the ruby tokens. 0 means plain centering.
    int spread = 0;
    // Gap before the first and after the last ruby token. Uncapped it is spread / 2n, but it is
    // limited to half a base character; the inner gaps absorb the remainder.
    int edge = 0;
    std::vector<size_t> breaks;  // token boundaries inside `text` (see CjkBreaks)
  };
  // Copies ruby token k of s (tokens are delimited by br) into out.
  const auto rubyToken = [](std::string_view s, const std::vector<size_t>& br, const size_t k, std::string& out) {
    const size_t b = k == 0 ? 0 : br[k - 1];
    const size_t e = k == br.size() ? s.size() : br[k];
    out.assign(s.data() + b, e - b);
  };
  const bool blockHasRuby = hasRuby();
  std::vector<RubyDrawInfo> rubies;
  if (blockHasRuby && !scanning) {
    rubies.resize(numWords);
    for (uint16_t i = 0; i < numWords; i++) {
      if (!rubyText(i).empty() && (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
        int groupWordCount = 1;
        while (i + groupWordCount < numWords && (wordStyle(i + groupWordCount) & EpdFontFamily::RUBY_CONTINUE) != 0) {
          groupWordCount++;
        }
        int groupActualWidth = 0;
        for (int k = 0; k < groupWordCount; ++k) {
          groupActualWidth += renderer.getTextAdvanceX(fontId, wordText(i + k), wordStyle(i + k), tracking);
        }
        // Whole-string width: the same measure layout uses to decide whether the ruby overhangs.
        const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyText(i).data(), EpdFontFamily::SUP, tracking);
        const int leaderWordX = wordXpos(i) + x;
        const auto baseDir =
            static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(wordText(i), blockStyle.isRtl ? 1 : 0));

        RubyDrawInfo& info = rubies[i];
        info.text = rubyText(i);
        info.baseDir = baseDir;
        info.x = leaderWordX - (rubyWidth - groupActualWidth) / 2;  // default: centered

        // CSS Ruby ruby-align: space-around. When the ruby is narrower than its base, the free space is
        // split so that each gap between ruby characters is twice the gap at either end; the centers
        // still coincide. A ruby with a single token (e.g. Latin text) degenerates to centering.
        if (rubyWidth < groupActualWidth) {
          auto breaks = CjkBreaks::characterBreakByteOffsets(rubyText(i));
          if (!breaks.empty()) {
            int tokenSum = 0;
            std::string tok;
            for (size_t k = 0; k <= breaks.size(); ++k) {
              rubyToken(info.text, breaks, k, tok);
              tokenSum += renderer.getTextAdvanceX(fontId, tok.c_str(), EpdFontFamily::SUP, tracking);
            }
            if (tokenSum < groupActualWidth) {
              // Count base characters (UTF-8 lead bytes) to get the average base character width.
              int baseChars = 0;
              for (int k = 0; k < groupWordCount; ++k) {
                for (const char* p = wordText(i + k); *p; ++p) {
                  if ((static_cast<uint8_t>(*p) & 0xC0) != 0x80) baseChars++;
                }
              }
              baseChars = std::max(baseChars, 1);
              // CSS Ruby: the start/end spacing is capped at half the size of one base character.
              const int edgeCap = groupActualWidth / (2 * baseChars);

              const int n = static_cast<int>(breaks.size()) + 1;
              info.x = leaderWordX;
              info.spread = groupActualWidth - tokenSum;
              info.edge = std::min(info.spread / (2 * n), edgeCap);
              info.breaks = std::move(breaks);
            }
          }
        }
        i += groupWordCount - 1;
      }
    }
  }

  struct DecorationLineTracker {
    EpdFontFamily::Style style;
    int startX = -1;
    int endX = -1;
    int yPos = 0;

    bool active() const { return startX != -1; }
    void reset() {
      startX = -1;
      endX = -1;
      yPos = 0;
    }
  };

  DecorationLineTracker decorationLines[] = {
      {EpdFontFamily::UNDERLINE},
      {EpdFontFamily::STRIKETHROUGH},
  };

  const auto flushDecoration = [&](DecorationLineTracker& line) {
    if (line.active()) {
      renderer.drawLine(line.startX, line.yPos, line.endX, line.yPos, 2, true);
      line.reset();
    }
  };
  const auto flushDecorations = [&]() {
    for (auto& line : decorationLines) {
      flushDecoration(line);
    }
  };

  // Loop-invariant: hoisted out of the word loop so rubyTexts is scanned once,
  // not once per word.
  const int rubyShift = getRubyShift(ascender);

  for (uint16_t i = 0; i < numWords; i++) {
    const char* word = wordText(i);
    const int wordX = wordXpos(i) + x;
    const EpdFontFamily::Style currentStyle = wordStyle(i);
    const auto baseDir =
        static_cast<BidiUtils::BidiBaseDir>(BidiUtils::detectParagraphLevel(word, blockStyle.isRtl ? 1 : 0));
    const uint8_t boundary = focusBoundary(i);
    const int wordFont = fontData ? renderFontId(renderer, wordFontId(i, sectionFontId), sectionFontId) : fontId;
    const int wordAscender = wordFont == fontId && !fontData ? ascender : renderer.getFontAscenderSize(wordFont);

    // SUP/SUB shift the baseline passed to drawText; the glyph is also scaled 50% inside
    // drawText, so these offsets are chosen relative to the full-size ascender:
    //   SUP: raise by 40% of ascender — sits clearly above the cap-height
    //   SUB: lower by 25% of ascender — descends below baseline without clashing with ascenders below
    int wordY = y + rubyShift + (ascender - wordAscender);
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    const int drawX = wordX;

    if (boundary > 0) {
      // Focus split: draw bold prefix, then the regular suffix at a pre-computed x offset.
      // The bold prefix is bounded to 9 codepoints by the clamp on targetBoldChars in
      // ParsedText::addWord; 9 UTF-8 codepoints occupy at most 9 * 4 = 36 bytes, +1 for null = 37.
      // suffixX is computed at cache-creation time to avoid font metric lookups at render time.
      static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
      char boldBuf[40];
      static_assert(sizeof(boldBuf) >= MAX_FOCUS_PREFIX_BYTES,
                    "boldBuf too small for max focus prefix (9 codepoints * 4 UTF-8 bytes + null)");
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen =
          std::min<size_t>({static_cast<size_t>(boundary), static_cast<size_t>(wordTextLen(i)), sizeof(boldBuf) - 1});
      memcpy(boldBuf, word, boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(wordFont, drawX, wordY, boldBuf, true, boldStyle, baseDir, tracking);
      const int suffixX = drawX + focusSuffixX(i);
      renderer.drawText(wordFont, suffixX, wordY, word + boldLen, true, currentStyle, baseDir, tracking);
    } else {
      renderer.drawText(wordFont, drawX, wordY, word, true, currentStyle, baseDir, tracking);
    }

    // Horizontal ruby text rendering
    if (blockHasRuby && !rubyText(i).empty() && (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
      if (scanning) {
        renderer.drawText(fontId, 0, 0, rubyText(i).data(), true, EpdFontFamily::SUP);
      } else {
        const int rubyY = wordY - ascender;
        const RubyDrawInfo& ruby = rubies[i];
        if (ruby.spread == 0) {
          renderer.drawText(fontId, ruby.x, rubyY, rubyText(i).data(), true, EpdFontFamily::SUP, ruby.baseDir,
                            tracking);
        } else {
          // The edge gap is capped (see layout), so the remaining space is split evenly over the n-1
          // gaps between tokens. Token k starts after the edge gap plus k inner gaps. Computed from k
          // rather than by accumulating a rounded gap, so rounding error does not build up.
          const int n = static_cast<int>(ruby.breaks.size()) + 1;  // n >= 2 here
          const int innerTotal = ruby.spread - 2 * ruby.edge;
          int pen = 0;
          std::string tok;
          for (int k = 0; k < n; ++k) {
            const int tokenIndex = ruby.baseDir == BidiUtils::BidiBaseDir::RTL ? n - 1 - k : k;
            rubyToken(ruby.text, ruby.breaks, static_cast<size_t>(tokenIndex), tok);
            const int gx = ruby.x + pen + ruby.edge + (innerTotal * k + (n - 1) / 2) / (n - 1);
            renderer.drawText(fontId, gx, rubyY, tok.c_str(), true, EpdFontFamily::SUP, ruby.baseDir, tracking);
            pen += renderer.getTextAdvanceX(fontId, tok.c_str(), EpdFontFamily::SUP, tracking);
          }
        }
      }
    }

    if (scanning) {
      continue;
    }

    if (EpdFontFamily::hasTextDecoration(currentStyle)) {
      int lineStartX = drawX;
      int lineWidth = renderer.getTextAdvanceX(wordFont, word, currentStyle, tracking, baseDir,
                                               GfxRenderer::TextMeasureMode::Rendered);

      // Do not decorate the synthetic em-space used for paragraph indentation.
      if (wordTextLen(i) >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
          static_cast<uint8_t>(word[2]) == 0x83) {
        const char* visibleText = word + 3;
        lineStartX += renderer.getTextAdvanceX(wordFont, "\xe2\x80\x83", currentStyle, tracking, baseDir,
                                               GfxRenderer::TextMeasureMode::Rendered);
        lineWidth = renderer.getTextAdvanceX(wordFont, visibleText, currentStyle, tracking, baseDir,
                                             GfxRenderer::TextMeasureMode::Rendered);
      }

      for (auto& line : decorationLines) {
        if ((currentStyle & line.style) == 0) {
          flushDecoration(line);
          continue;
        }

        int offset = 2;
        if (line.style == EpdFontFamily::STRIKETHROUGH) {
          int32_t advance = 0;
          int height = 0;
          const bool smallCaps = (currentStyle & EpdFontFamily::SMALL_CAPS) != 0;
          if (!renderer.getCodepointMetrics(wordFont, smallCaps ? 'X' : 'x', currentStyle, advance, height) ||
              height <= 0) {
            height = wordAscender * 2 / 3;
          }
          if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
            height = (height + 1) / 2;
          } else if (smallCaps) {
            height = (height * 3 + 3) / 4;
          }
          offset = -std::max(1, height / 2);
        }
        const int lineY = wordY + wordAscender + offset;
        if (line.active() && line.yPos != lineY) {
          flushDecoration(line);
        }
        if (!line.active()) {
          line.startX = lineStartX;
          line.yPos = lineY;
        }
        line.endX = lineStartX + lineWidth;
      }
    } else {
      flushDecorations();
    }
  }
  flushDecorations();
}

bool TextBlock::serialize(HalFile& file) const {
  const auto& blockStyle = getBlockStyle();
  if (!isValid) {
    LOG_ERR("TXB", "Serialization failed: invalid block");
    return false;
  }

  // Word data: scalars, then the arena verbatim -- its in-memory layout is
  // exactly the on-disk layout (see TextBlock.h), so one write covers all
  // per-word arrays and the text blob.
  if (!sectioncodec::write(
          file, sectioncodec::record(XP_SEC_TEXT_HEADER, {numWords, focusPresent, textBytes, paragraphStartWord})))
    return false;
  if (numWords > 0) {
    const size_t size = xp_sec_arena_size(numWords, focusPresent, textBytes);
    if (file.write(sourceRanges, size) != size) {
      LOG_ERR("TXB", "Serialization failed: arena write (%u bytes)", static_cast<uint32_t>(size));
      return false;
    }
  }

  // Ruby text data
  for (size_t i = 0; i < numWords; i++) {
    const auto ruby = rubyText(i);
    if (!sectioncodec::writeValue(file, XP_SEC_U32, ruby.size()) || file.write(ruby.data(), ruby.size()) != ruby.size())
      return false;
  }

  // Style (alignment + margins/padding/indent)
  if (!sectioncodec::write(
          file, sectioncodec::record(XP_SEC_BLOCK_STYLE,
                                     {uint32_t(blockStyle.alignment), uint32_t(blockStyle.textAlignDefined),
                                      uint32_t(blockStyle.marginTop), uint32_t(blockStyle.marginBottom),
                                      uint32_t(blockStyle.marginLeft), uint32_t(blockStyle.marginRight),
                                      uint32_t(blockStyle.paddingTop), uint32_t(blockStyle.paddingBottom),
                                      uint32_t(blockStyle.paddingLeft), uint32_t(blockStyle.paddingRight),
                                      uint32_t(blockStyle.textIndent), uint32_t(blockStyle.textIndentDefined),
                                      uint32_t(blockStyle.isRtl), uint32_t(blockStyle.directionDefined),
                                      uint32_t(blockStyle.characterSpacing), uint32_t(blockStyle.fontId)})))
    return false;

  // Per-word fonts: a flag, then the font table and one slot per word.
  const uint8_t hasWordFonts = fontData ? 1 : 0;
  if (!sectioncodec::writeValue(file, XP_SEC_WORD_FONT_FLAG, hasWordFonts)) return false;
  if (hasWordFonts) {
    const size_t size = sizeof(int32_t) * MAX_WORD_FONTS + numWords;
    if (file.write(fontData, size) != size) {
      LOG_ERR("TXB", "Serialization failed: word fonts");
      return false;
    }
  }

  return true;
}

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
  const TextBlock source(View{}, page, size, element);
  if (!source.valid()) return false;
  out = {source.blockStyle, source.sourceRanges,       source.textOffArr, source.xposArr,      source.focusSuffixXArr,
         source.stylesArr,  source.focusBoundaryArr,   source.textArr,    source.fontData,     source.rubyOffsets,
         source.numWords,   source.paragraphStartWord, source.textBytes,  source.focusPresent, source.pageHasRuby};
  return true;
}

TextBlock::TextBlock(View, const uint8_t* page, const ViewData& data)
    : blockStyle(data.style),
      numWords(data.count),
      paragraphStartWord(data.paragraphStart),
      textBytes(data.textBytes),
      focusPresent(data.focus),
      sourceRanges(data.sources),
      textOffArr(data.offsets),
      xposArr(data.positions),
      focusSuffixXArr(data.suffixes),
      stylesArr(data.styles),
      focusBoundaryArr(data.boundaries),
      textArr(data.text),
      pageArena(page),
      rubyOffsets(data.rubies),
      pageHasRuby(data.ruby),
      fontData(data.fonts) {}

std::string_view TextBlock::pageRubyText(const uint16_t i) const {
  if (i >= numWords) return "";
  const uint32_t offset = unaligned(reinterpret_cast<const uint32_t*>(rubyOffsets + size_t(i) * 4));
  const uint8_t* record = pageArena + offset;
  const uint32_t length = unaligned(reinterpret_cast<const uint32_t*>(record));
  return {reinterpret_cast<const char*>(record + 4), length};
}
