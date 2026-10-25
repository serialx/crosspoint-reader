#include "ParsedText.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string_view>
#include <vector>

#include "CjkBreaks.h"
#include "TokenBoundary.h"
#include "hyphenation/HyphenationCommon.h"
#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

#ifndef XP_RUST_LAYOUT
#define XP_RUST_LAYOUT 0
#endif

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
#if !XP_RUST_LAYOUT
constexpr size_t SOFT_HYPHEN_BYTES = 2;
#endif
// Paragraph-level direction: scan the first N words to find base direction.
constexpr size_t RTL_PARAGRAPH_PROBE_WORDS = 3;
// Per-word: scan enough chars to see through leading neutrals (quotes, numbers)
// before giving up. 64 is a hedge for pathological cases like long numeric tokens.
constexpr int RTL_PER_WORD_PROBE_DEPTH = 64;
#if !XP_RUST_LAYOUT
constexpr size_t MIN_JUSTIFY_GAPS = 1;
#endif

// Byte-level pre-check: Hebrew UTF-8 lead bytes 0xD6-0xD7, Arabic/Syriac 0xD8-0xDB.
bool mayContainRtlBytes(const char* str) {
  for (const auto* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
    if (*p >= 0xD6 && *p <= 0xDB) return true;
  }
  return false;
}

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string_view word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.data());
  const auto* const end = ptr + word.size();
  while (ptr < end) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
  return 0;
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepoint(const std::string_view word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.data() + i);
  return utf8NextCodepoint(&ptr);
}

bool containsSoftHyphen(const std::string_view word) { return word.find(SOFT_HYPHEN_UTF8) != std::string_view::npos; }

bool containsCjkBreakableCodepoint(const std::string& text) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*ptr) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (utf8IsCjkBreakable(cp)) {
      return true;
    }
  }
  return false;
}

uint32_t countCodepoints(const std::string_view text) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.data());
  const auto* const end = ptr + text.size();
  uint32_t count = 0;
  while (ptr < end) {
    utf8NextCodepoint(&ptr);
    count++;
  }
  return count;
}

#if !XP_RUST_LAYOUT
// Line-end split points inside a Hangul word, using the CJK boundary rules (no hyphen is drawn).
std::vector<Hyphenator::BreakInfo> hangulLineEndBreaks(const std::string& word) {
  std::vector<Hyphenator::BreakInfo> breaks;
  if (word.empty()) return breaks;
  const auto* const start = reinterpret_cast<const unsigned char*>(word.c_str());
  const auto* ptr = start;
  uint32_t prev = utf8NextCodepoint(&ptr);
  while (*ptr) {
    const size_t offset = static_cast<size_t>(ptr - start);
    const uint32_t cur = utf8NextCodepoint(&ptr);
    if ((utf8IsHangul(prev) || utf8IsHangul(cur)) && CjkBreaks::boundaryAllowsBreak(prev, cur)) {
      breaks.push_back({offset, false});
    }
    prev = cur;
  }
  return breaks;
}

#endif

// Scans text for explicit hyphens or dashes and returns byte offsets where a line break is permitted right after
// the hyphen/dash. Multi-character hyphen/dash runs are kept together as an indivisible unit.
std::vector<size_t> explicitHyphenBreakByteOffsets(const std::string& text, const bool attachToPrevious) {
  std::vector<size_t> breakOffsets;
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* const start = ptr;
  const auto* const end = ptr + text.size();

  bool hasLeadingContent = attachToPrevious;

  while (ptr < end) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;

    if (TokenBoundary::allowsBreakAfterExplicitHyphen(cp)) {
      while (ptr < end) {
        const auto* const nextStart = ptr;
        const uint32_t nextCp = utf8NextCodepoint(&ptr);
        if (!TokenBoundary::allowsBreakAfterExplicitHyphen(nextCp)) {
          ptr = nextStart;
          break;
        }
      }

      if (hasLeadingContent && ptr < end) {
        breakOffsets.push_back(static_cast<size_t>(ptr - start));
      }
      hasLeadingContent = true;
    } else {
      hasLeadingContent = true;
    }
  }

  return breakOffsets;
}

#if !XP_RUST_LAYOUT
int computeJustifyExtra(const int spareSpace, const size_t gapCount) {
  if (gapCount < MIN_JUSTIFY_GAPS || spareSpace <= 0) return 0;
  // Distribute the spare space evenly across gaps. Do NOT bail out to 0 when the
  // per-gap stretch is large: a sparse line (few words on a wide page) legitimately
  // needs big gaps to reach the margin. Returning 0 there disables justification for
  // that line, leaving it right-aligned (RTL) / left-aligned (LTR) — the mismatched
  // alignment bug. Match the un-capped behavior of the old code.
  return spareSpace / static_cast<int>(gapCount);
}

#endif

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
#if XP_RUST_LAYOUT
  word.resize(xp_layout_strip_soft_hyphens(reinterpret_cast<uint8_t*>(word.data()), word.size()));
#else
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
#endif
}

constexpr int scaleSpace(const int advance, const uint8_t wordSpacingPercent) {
  return wordSpacingPercent == 100 ? advance : (advance * wordSpacingPercent + 50) / 100;
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
// `word` is usually NUL-terminated at data()[size()] (arena entries and
// std::string arguments), but focus-split candidates pass PREFIX views of a
// stored word. Probing data()[size()] is in-bounds for every caller (it is
// either the word's own NUL or a mid-word byte of the same arena entry) and
// routes unterminated views through the copying path so the C API never
// measures past the view.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string_view word,
                          const EpdFontFamily::Style style, const int8_t characterSpacing,
                          const uint8_t wordSpacingPercent, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return scaleSpace(renderer.getSpaceWidth(fontId, style), wordSpacingPercent);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen && word.data()[word.size()] == '\0') {
    return renderer.getTextAdvanceX(fontId, word.data(), style, characterSpacing);
  }

  std::string sanitized(word);
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style, characterSpacing);
}

// True when a token ends with a hyphen or dash that allows a line break after it. U+00AD is
// excluded because it renders as nothing, so only a hyphenation break -- which substitutes a
// visible '-' -- may use it; U+2011 because forbidding that break is its purpose.
bool endsWithBreakableHyphen(const std::string_view token) {
  if (token.empty()) return false;
  return TokenBoundary::allowsBreakAfterExplicitHyphen(lastCodepoint(token));
}

// Focus Reading renders the first `focusBoundary` bytes of a token bold and the rest at the
// token's own style; 0 means no emphasis. The bold run is at most 9 codepoints (see addWord),
// so it is copied to a stack buffer rather than a substring: this runs once per word during
// pagination and must not allocate.
constexpr size_t FOCUS_PREFIX_BUF_SIZE = 40;

// Advance from the token origin to the start of its regular-weight suffix: the bold prefix plus
// the kerning across the weight change. Doubles as the suffix's x offset inside the word.
uint16_t measureFocusPrefixAdvance(const GfxRenderer& renderer, const int fontId, const std::string_view word,
                                   const EpdFontFamily::Style style, const uint8_t focusBoundary,
                                   const int8_t characterSpacing) {
  char prefixBuf[FOCUS_PREFIX_BUF_SIZE];
  const size_t prefixLen = std::min<size_t>(focusBoundary, FOCUS_PREFIX_BUF_SIZE - 1);
  memcpy(prefixBuf, word.data(), prefixLen);
  prefixBuf[prefixLen] = '\0';

  const auto boldStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
  const auto* suffixPtr = reinterpret_cast<const unsigned char*>(word.data() + focusBoundary);
  const int kerning =
      renderer.getKerning(fontId, lastCodepoint(prefixBuf), utf8NextCodepoint(&suffixPtr), boldStyle, characterSpacing);
  return static_cast<uint16_t>(renderer.getTextAdvanceX(fontId, prefixBuf, boldStyle, characterSpacing) + kerning);
}

// Advance width of a whole token, accounting for a bold focus prefix when it has one.
uint16_t measureFocusWordWidth(const GfxRenderer& renderer, const int fontId, const std::string_view word,
                               const EpdFontFamily::Style style, const uint8_t focusBoundary,
                               const int8_t characterSpacing, const uint8_t wordSpacingPercent,
                               const bool appendHyphen = false) {
  if (focusBoundary == 0) {
    return measureWordWidth(renderer, fontId, word, style, characterSpacing, wordSpacingPercent, appendHyphen);
  }
  if (focusBoundary >= word.size()) {
    // The bold run covers the whole token, as for a split candidate ending on the boundary.
    return measureWordWidth(renderer, fontId, word, static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD),
                            characterSpacing, wordSpacingPercent, appendHyphen);
  }
  // Through measureWordWidth (not getTextAdvanceX directly): `word` itself can
  // be an unterminated prefix view here, and the suffix view inherits that.
  const uint16_t suffixWidth = measureWordWidth(renderer, fontId, word.substr(focusBoundary), style, characterSpacing,
                                                wordSpacingPercent, appendHyphen);
  return measureFocusPrefixAdvance(renderer, fontId, word, style, focusBoundary, characterSpacing) + suffixWidth;
}

#if !XP_RUST_LAYOUT
// Focus boundaries for the two halves of a token split at `splitOffset`.
uint8_t focusBoundaryBefore(const uint8_t focusBoundary, const size_t splitOffset) {
  return static_cast<uint8_t>(std::min<size_t>(focusBoundary, splitOffset));
}
uint8_t focusBoundaryAfter(const uint8_t focusBoundary, const size_t splitOffset) {
  return focusBoundary > splitOffset ? static_cast<uint8_t>(focusBoundary - splitOffset) : 0;
}
#endif

// Checks if a UTF-8 codepoint should be counted as part of a word for Focus Reading
bool isWordCharacter(uint32_t cp) {
  // ASCII range (Catches 95%+ of characters immediately)
  if (cp < 128) {
    // Bitwise trick: (cp | 0x20) converts uppercase ASCII to lowercase.
    // This checks for A-Z and a-z mathematically, avoiding memory lookups and <cctype>
    return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') || cp == '\'';
  }

  // General Punctuation Block, Currency, Math, Arrows, & Symbols (0x2000 - 0x2BFF)
  if (cp >= 0x2000 && cp <= 0x2BFF) {
    // Explicitly allow smart quotes, reject all other general punctuation (em-dashes, etc.)
    return cp == 0x2018 || cp == 0x2019;
  }

  // Latin-1 Punctuation Block (0x00A1 - 0x00BF)
  if (cp >= 0x00A1 && cp <= 0x00BF) {
    // Allow ordinal indicators and micro sign, reject the rest (¡, ¿, «, », etc.)
    return cp == 0x00AA || cp == 0x00B5 || cp == 0x00BA;
  }

  // Rejects Two-em dash, Three-em dash, Double oblique hyphen, etc.
  if (cp >= 0x2E00 && cp <= 0x2E7F) return false;

  // Rejects Modifier Minus (0x02D7), Small Hyphen (0xFE63), and Fullwidth Hyphen (0xFF0D)
  if (cp == 0x02D7 || cp == 0xFE63 || cp == 0xFF0D) return false;
  // Assume all other Unicode ranges (accented letters, Cyrillic, Greek, etc.) are valid

  return true;
}

}  // namespace

uint32_t ParsedText::visibleOffsetAt(const size_t wordIndex) const { return xp_tokens_offset(tokens(), wordIndex); }

uint32_t ParsedText::sourceOffsetAfter(const uint32_t start, const uint32_t renderedLength) const {
  return xp_tokens_source_after(tokens(), start, renderedLength);
}

bool ParsedText::recordAbsorbedSourceOffset(const uint32_t offset) {
  if (xp_tokens_record_absorbed(tokens(), offset)) return true;
  LOG_ERR("PTX", "OOM: NFC source offsets");
  droppedWords = true;
  return false;
}

bool ParsedText::storeWord(const std::string_view text, WordStore::StoredWord& out) {
  if (wordStore.append(text.data(), text.size(), out)) return true;
  if (!droppedWords) {
    LOG_ERR("PTX", "OOM: dropping paragraph text (arena chunk alloc failed)");
  }
  droppedWords = true;
  return false;
}

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious, const uint32_t visibleTextOffset, const uint8_t linkId,
                         const uint8_t sizeSlot, const bool noSpaceBefore) {
  if (word.empty()) return;

  // The device fonts carry no combining-mark positioning, so EPUB text stored in NFD
  // (a base letter followed by separate combining accents -- common for Vietnamese,
  // and used for many EPUB <h1> chapter headings) renders with the marks detached or
  // misplaced. Compose to NFC here, the single funnel every word passes through, so a
  // precomposed glyph is used instead. This runs once per word at layout time (the
  // result is cached in the section file) and is a cheap no-op for mark-free text.
  struct SourceContext {
    ParsedText* text;
    uint32_t start;
  } sourceContext{this, visibleTextOffset};
  word = utf8ComposeNfc(
      word,
      [](const uint32_t offset, void* context) {
        const auto& source = *static_cast<SourceContext*>(context);
        return source.text->recordAbsorbedSourceOffset(source.start + offset);
      },
      &sourceContext);
  if (word.empty()) return;

  EpdFontFamily::Style baseStyle = fontStyle;
  if (underline) {
    baseStyle = static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::UNDERLINE);
  }
  const bool wordStartsRtl = !hasRtlWord && mayContainRtlBytes(word.c_str()) &&
                             BidiUtils::startsWithRtl(word.c_str(), RTL_PER_WORD_PROBE_DEPTH);

  // Metadata insertion is atomic; on failure release the newly stored text and fail the paragraph.
  const auto pushStyledToken = [&](std::string_view token, const EpdFontFamily::Style style, const bool continues,
                                   const bool noSpaceBefore, const uint8_t focusBoundary, const uint32_t tokenOffset,
                                   const bool padRuby) {
    if (droppedWords) return;
    WordStore::StoredWord stored;
    if (!storeWord(token, stored)) return;
    const XpToken metadata = {
        {stored.chunk, stored.off, stored.len},
        static_cast<uint8_t>(style & ~EpdFontFamily::SMALL_CAPS),
        focusBoundary,
        linkId,
        static_cast<uint8_t>(sizeSlot | ((style & EpdFontFamily::SMALL_CAPS) ? TextBlock::SMALL_CAPS_SLOT_FLAG : 0)),
        static_cast<uint8_t>((continues ? 1 : 0) | (noSpaceBefore ? 2 : 0))};
    if (!xp_tokens_insert(tokens(), tokenCount(), &metadata, tokenOffset)) {
      wordStore.release(stored);
      LOG_ERR("PTX", "OOM: token metadata");
      droppedWords = true;
      return;
    }
    if (padRuby && !rubyTexts.empty()) rubyTexts.push_back("");
  };

  const auto pushToken = [&](std::string_view token, const bool continues, const bool noSpaceBefore,
                             const uint8_t focusBoundary, const uint32_t tokenOffset) {
    pushStyledToken(token, baseStyle, continues, noSpaceBefore, focusBoundary, tokenOffset, true);
  };

  bool effectiveAttachToPrevious = attachToPrevious;
  bool effectiveNoSpaceBefore = noSpaceBefore;
  // Only a glued token (attachToPrevious == true, i.e. no whitespace separated it from the
  // previous one in the source) may be turned into a gap-less break opportunity. When real
  // whitespace separated the two words, that space is content and must be rendered: Korean
  // is a space-delimited script written in Hangul, which utf8IsCjkBreakable() covers.
  if (attachToPrevious && !(tokenCount() == 0) &&
      endsWithBreakableHyphen(wordStore.view(storedWordAt(tokenCount() - 1)))) {
    effectiveAttachToPrevious = true;
    effectiveNoSpaceBefore = true;
  } else if (attachToPrevious && !(tokenCount() == 0) &&
             CjkBreaks::hasBreakOpportunityBetween(lastCodepoint(wordStore.view(storedWordAt(tokenCount() - 1))),
                                                   firstCodepoint(word))) {
    effectiveAttachToPrevious = false;
    effectiveNoSpaceBefore = true;
  }

  const auto ensureTokenCapacity = [&](const size_t additionalTokens) {
    if (additionalTokens > SIZE_MAX - tokenCount() || !xp_tokens_reserve(tokens(), tokenCount() + additionalTokens)) {
      LOG_ERR("PTX", "OOM: token metadata reserve");
      droppedWords = true;
    }
  };

  if (auto breakOffsets = CjkBreaks::characterBreakByteOffsets(word); !breakOffsets.empty()) {
    // CJK-heavy paragraphs can push hundreds of tiny tokens quickly when CSS toggles
    // inline styles. Reserve once up front to avoid repeated vector growth reallocations.
    ensureTokenCapacity(breakOffsets.size() + 1);
    bool firstToken = true;
    size_t tokenStart = 0;
    uint32_t tokenVisibleOffset = visibleTextOffset;
    for (const size_t breakOffset : breakOffsets) {
      if (breakOffset <= tokenStart || breakOffset > word.size()) continue;
      const std::string_view token(word.data() + tokenStart, breakOffset - tokenStart);
      const bool attach = firstToken ? effectiveAttachToPrevious
                                     : (!(tokenCount() == 0) &&
                                        endsWithBreakableHyphen(wordStore.view(storedWordAt(tokenCount() - 1))));
      pushToken(token, attach, firstToken ? effectiveNoSpaceBefore : true,
                /*focusBoundary=*/0, tokenVisibleOffset);
      tokenVisibleOffset = sourceOffsetAfter(tokenVisibleOffset, countCodepoints(token));
      firstToken = false;
      tokenStart = breakOffset;
    }
    if (tokenStart < word.size()) {
      const bool attach = firstToken ? effectiveAttachToPrevious
                                     : (!(tokenCount() == 0) &&
                                        endsWithBreakableHyphen(wordStore.view(storedWordAt(tokenCount() - 1))));
      pushToken(std::string_view(word).substr(tokenStart), attach, firstToken ? effectiveNoSpaceBefore : true,
                /*focusBoundary=*/0, tokenVisibleOffset);
    }
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  if (containsCjkBreakableCodepoint(word)) {
    pushToken(word, effectiveAttachToPrevious, effectiveNoSpaceBefore, /*focusBoundary=*/0, visibleTextOffset);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // Already-bold text should stay fully bold; focus splitting would make its suffix regular later.
  if (!this->focusReadingEnabled || (baseStyle & EpdFontFamily::BOLD) != 0) {
    if (auto breakOffsets = explicitHyphenBreakByteOffsets(word, effectiveAttachToPrevious); !breakOffsets.empty()) {
      ensureTokenCapacity(breakOffsets.size() + 1);
      bool firstToken = true;
      size_t tokenStart = 0;
      uint32_t tokenVisibleOffset = visibleTextOffset;
      for (const size_t breakOffset : breakOffsets) {
        if (breakOffset <= tokenStart || breakOffset > word.size()) continue;
        const std::string_view token(word.data() + tokenStart, breakOffset - tokenStart);
        pushToken(token, firstToken ? effectiveAttachToPrevious : true, firstToken ? effectiveNoSpaceBefore : true,
                  /*focusBoundary=*/0, tokenVisibleOffset);
        tokenVisibleOffset = sourceOffsetAfter(tokenVisibleOffset, countCodepoints(token));
        firstToken = false;
        tokenStart = breakOffset;
      }
      if (tokenStart < word.size()) {
        pushToken(std::string_view(word).substr(tokenStart), firstToken ? effectiveAttachToPrevious : true,
                  firstToken ? effectiveNoSpaceBefore : true, /*focusBoundary=*/0, tokenVisibleOffset);
      }
      if (wordStartsRtl) {
        hasRtlWord = true;
      }
      return;
    }

    pushToken(word, effectiveAttachToPrevious, effectiveNoSpaceBefore, /*focusBoundary=*/0, visibleTextOffset);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // --- FOCUS READING LOGIC BELOW ---

  // Worst case: a segment boundary on each byte (highly punctuated UTF-8 text).
  ensureTokenCapacity(word.length());

  // Lambda helper to process and push individual sub-segments of the string
  // Use std::string_view to avoid heap allocations when slicing
  auto processSegment = [&](std::string_view segment, bool isWord, bool attach, bool noSpaceBefore) {
    const unsigned char* wordBegin = reinterpret_cast<const unsigned char*>(word.data());
    const unsigned char* segmentBegin = reinterpret_cast<const unsigned char*>(segment.data());
    uint32_t segmentLength = 0;
    const unsigned char* offsetPtr = wordBegin;
    while (offsetPtr < segmentBegin) {
      utf8NextCodepoint(&offsetPtr);
      segmentLength++;
    }
    const uint32_t segmentOffset = sourceOffsetAfter(visibleTextOffset, segmentLength);
    if (!isWord) {
      // Punctuation and Numbers stay regular
      pushStyledToken(segment, baseStyle, attach, noSpaceBefore, /*focusBoundary=*/0, segmentOffset, false);
    } else {
      size_t charCount = 0;
      const unsigned char* countPtr = reinterpret_cast<const unsigned char*>(segment.data());
      const unsigned char* countEnd = countPtr + segment.length();

      while (countPtr < countEnd) {
        utf8NextCodepoint(&countPtr);
        charCount++;
      }

      // Target 45% for 1-bold at 4 chars and 3-bold at 7 chars with floor truncation
      constexpr size_t FOCUS_READING_PERCENT = 45;
      size_t targetBoldChars = (charCount * FOCUS_READING_PERCENT) / 100;
      targetBoldChars = std::clamp<size_t>(targetBoldChars, 1, 9);

      if (targetBoldChars >= charCount) {
        // Whole segment is bold - no suffix split needed
        pushStyledToken(segment, static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::BOLD), attach,
                        noSpaceBefore, /*focusBoundary=*/0, segmentOffset, false);
      } else {
        countPtr = reinterpret_cast<const unsigned char*>(segment.data());
        for (size_t i = 0; i < targetBoldChars; ++i) {
          utf8NextCodepoint(&countPtr);
        }
        size_t splitByteOffset = countPtr - reinterpret_cast<const unsigned char*>(segment.data());

        // One token carrying the emphasis as a byte boundary, so the word stays whole for the
        // hyphenator and the line breaker. The renderer applies BOLD to bytes [0, splitByteOffset).
        pushStyledToken(segment, baseStyle, attach, noSpaceBefore,
                        static_cast<uint8_t>(std::min<size_t>(splitByteOffset, 255)), segmentOffset, false);
      }
    }
  };

  // Tokenize the string by alternating states (Word vs. Non-Word)
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* end = ptr + word.length();

  const unsigned char* segmentStart = ptr;
  uint32_t firstCp = utf8NextCodepoint(&ptr);  // Consume the first char to determine initial state
  bool inWordSegment = isWordCharacter(firstCp);

  bool isFirstSegment = true;

  while (ptr < end) {
    const unsigned char* currentCpStart = ptr;
    uint32_t cp = utf8NextCodepoint(&ptr);
    bool isWordChar = isWordCharacter(cp);

    // Whenever the character type flips, slice off the segment we just completed and process it
    if (isWordChar != inWordSegment) {
      size_t segmentLen = currentCpStart - segmentStart;
      std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);

      // Only the very first segment inherits the original attachToPrevious flag.
      // Every subsequent segment glues seamlessly to the prefix. After a visible explicit-hyphen
      // character, continues=true + noSpaceBefore=true records a breakable attachment: it may wrap,
      // but when it stays on the line it receives kerning only, never a space or justification.
      const bool breakAfterPrev = !isFirstSegment && !(tokenCount() == 0) &&
                                  endsWithBreakableHyphen(wordStore.view(storedWordAt(tokenCount() - 1)));
      processSegment(segment, inWordSegment, isFirstSegment ? effectiveAttachToPrevious : true,
                     isFirstSegment ? effectiveNoSpaceBefore : breakAfterPrev);

      // Setup for the next segment
      segmentStart = currentCpStart;
      inWordSegment = isWordChar;
      isFirstSegment = false;
    }
  }

  // Process the final remaining segment
  size_t segmentLen = end - segmentStart;
  std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);
  const bool breakAfterPrev = !isFirstSegment && !(tokenCount() == 0) &&
                              endsWithBreakableHyphen(wordStore.view(storedWordAt(tokenCount() - 1)));
  processSegment(segment, inWordSegment, isFirstSegment ? effectiveAttachToPrevious : true,
                 isFirstSegment ? effectiveNoSpaceBefore : breakAfterPrev);
  if (wordStartsRtl) {
    hasRtlWord = true;
  }
}

uint8_t ParsedText::addLinkTarget(const char* href) {
  if (!href || href[0] == '\0' || strnlen(href, FOOTNOTE_HREF_LEN) >= FOOTNOTE_HREF_LEN ||
      linkTargets.size() >= UINT8_MAX) {
    return 0;
  }
  linkTargets.emplace_back(href);
  return static_cast<uint8_t>(linkTargets.size());
}

bool ParsedText::linkTargetMatches(const uint8_t linkId, const char* href) const {
  return linkId > 0 && linkId <= linkTargets.size() && href && linkTargets[linkId - 1] == href;
}

void ParsedText::setRubyForWordAt(size_t index, const std::string& ruby) {
  if (index >= tokenCount()) return;
  if (rubyTexts.size() <= index) {
    rubyTexts.resize(tokenCount());
  }
  rubyTexts[index] = ruby;
}

void ParsedText::setRubyGroupAt(size_t startIndex, size_t count, const std::string& ruby) {
  if (startIndex >= tokenCount()) return;
  if (rubyTexts.size() <= startIndex) {
    rubyTexts.resize(tokenCount());
  }
  rubyTexts[startIndex] = ruby;
  for (size_t i = 1; i < count; i++) {
    size_t idx = startIndex + i;
    if (idx >= tokenCount()) break;
    if (rubyTexts.size() <= idx) {
      rubyTexts.resize(tokenCount());
    }
    rubyTexts[idx] = "";
    auto token = tokenAt(idx);
    token.style |= EpdFontFamily::RUBY_CONTINUE;
    token.flags = 1;  // unbreakable attachment
    xp_tokens_set(tokens(), idx, &token);
  }
}

void ParsedText::ensureRubyCapacity() {
  // No-op: rubyTexts is a std::deque (chunked growth, no capacity to pre-reserve
  // and no large contiguous reallocation to avoid). Kept for call-site stability.
}

int ParsedText::resolveFirstLineIndent(const bool isFirstLine, const GfxRenderer& renderer, const int fontId) const {
  if (firstLineConsumed || !isFirstLine || !isNaturalAlign) {
    return 0;
  }
  if (blockStyle.textIndentDefined && blockStyle.textIndent < 0) return blockStyle.textIndent;
  if (paragraphIndentSpaces == 0) return 0;
  return scaleSpace(renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR), wordSpacingPercent) * paragraphIndentSpaces;
}
// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                                       const bool includeLastLine, const int8_t characterSpacing,
                                       const uint8_t wordSpacingPercent, const size_t maxLines,
                                       const bool* stopRequested) {
  if ((tokenCount() == 0) || maxLines == 0 || (stopRequested && *stopRequested)) {
    return;
  }
  // Stamped here rather than at construction: the parser replaces blockStyle as CSS resolves.
  blockStyle.characterSpacing = characterSpacing;
  this->wordSpacingPercent = wordSpacingPercent;

  // Per-paragraph RTL auto-detection: only when CSS/HTML didn't explicitly set direction.
  // Explicit dir="ltr" must be respected and not overridden by content heuristic.
  if (!blockStyle.directionDefined && hasRtlWord) {
    // Check the first few words for RTL letter codepoints (no heap allocation).
    const size_t wordsToScan = std::min(tokenCount(), RTL_PARAGRAPH_PROBE_WORDS);
    for (size_t i = 0; i < wordsToScan; ++i) {
      if (BidiUtils::startsWithRtl(wordStore.cstr(storedWordAt(i)), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH)) {
        blockStyle.isRtl = true;
        break;
      }
    }
  }

  isNaturalAlign =
      blockStyle.alignment == CssTextAlign::Justify ||
      (blockStyle.isRtl ? blockStyle.alignment == CssTextAlign::Right : blockStyle.alignment == CssTextAlign::Left);

  // Ensure SD card font glyph metrics are loaded before measuring word widths.
  // For flash-based fonts isSdCardFont() returns false and this block is skipped
  // entirely — no heap allocation. For SD card fonts this reads glyph metadata
  // (advanceX only, no bitmaps) for all unique codepoints in this paragraph so
  // that calculateWordWidths() can measure text without on-demand SD I/O.
  // Every size in the paragraph measures through its own font: the block font plus any
  // inline-size slots resolved by the parser.
  int32_t paragraphFonts[MAX_SIZE_SLOTS] = {fontId};
  size_t paragraphFontCount = 1;
  for (uint8_t slot = 1; slot < sizeSlotCount; ++slot) {
    const int32_t slotFont = sizeSlotFontIds[slot];
    if (slotFont != 0 && std::find(paragraphFonts, paragraphFonts + paragraphFontCount, slotFont) ==
                             paragraphFonts + paragraphFontCount) {
      paragraphFonts[paragraphFontCount++] = slotFont;
    }
  }
  const bool anySdFont = std::any_of(paragraphFonts, paragraphFonts + paragraphFontCount,
                                     [&renderer](const int32_t font) { return renderer.isSdCardFont(font); });
  if (anySdFont) {
    // Style mask: only ask the SD font to load advances for styles actually
    // used in this paragraph. Style index is the low two bits (regular/bold/
    // italic/bold-italic); the underline bit is irrelevant to advance metrics.
    uint8_t styleMask = 0;
    bool includeSmallCaps = false;
    for (size_t i = 0; i < tokenCount(); ++i) {
      const auto token = tokenAt(i);
      styleMask |= static_cast<uint8_t>(1u << (token.style & 0x03));
      includeSmallCaps |= (token.slot & TextBlock::SMALL_CAPS_SLOT_FLAG) != 0;
    }
    if (styleMask == 0) styleMask = 0x01;
    // Hand the arena chunks over as packed NUL-separated word runs. Two small
    // pointer tables (~8 B per live chunk) instead of per-word iteration.
    std::vector<const char*> segments;
    std::vector<size_t> segmentLens;
    segments.reserve(wordStore.chunkCount());
    segmentLens.reserve(wordStore.chunkCount());
    for (size_t i = 0; i < wordStore.chunkCount(); ++i) {
      const char* data = wordStore.chunkData(i);
      if (!data) continue;  // retired chunk
      segments.push_back(data);
      segmentLens.push_back(wordStore.chunkUsed(i));
    }
    for (size_t i = 0; i < paragraphFontCount; ++i) {
      if (!renderer.isSdCardFont(paragraphFonts[i])) continue;
      renderer.ensureSdCardFontReady(paragraphFonts[i], segments.data(), segmentLens.data(), segments.size(),
                                     tokenCount() > 1, hyphenationEnabled, styleMask, includeSmallCaps);
    }
  }

  const int pageWidth = viewportWidth;
  auto wordWidths = calculateWordWidths(renderer, fontId);
#if XP_RUST_LAYOUT
  if (!wordWidths.valid()) {
    droppedWords = true;
    return;
  }
#endif

  ParagraphBreaks lineBreakIndices;
#if XP_RUST_LAYOUT
  lineBreakIndices = computeRustLineBreaks(renderer, fontId, pageWidth, wordWidths, maxLines);
  if (lineBreakIndices.empty() || !wordWidths.valid()) {
    LOG_ERR("PTX", "OOM: paragraph line breaks");
    droppedWords = true;
    return;
  }
#else
  if (hyphenationEnabled || maxLines != SIZE_MAX) {
    // Bounded layout must leave words beyond the emitted lines unsplit for a later width.
    lineBreakIndices = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, wordWidths, maxLines);
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, wordWidths);
  }
#endif
  const bool allLinesComputed = lineBreakIndices.back() == tokenCount();
  const size_t lineCount = lineBreakIndices.size() - (!includeLastLine && allLinesComputed ? 1 : 0);

  const bool endIsKnown = includeLastLine && allLinesComputed;
  for (size_t i = 0; i < lineCount; ++i) {
    if (stopRequested && *stopRequested) return;
    linesAfterCurrent = endIsKnown ? static_cast<int>(lineCount - 1 - i) : -1;
    extractLine(i, pageWidth, wordWidths, lineBreakIndices, processLine, renderer, fontId);
  }
  linesAfterCurrent = -1;

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    firstLineConsumed = true;
    paragraphStartEmitted = true;

    const size_t consumed = lineBreakIndices[lineCount - 1];
    for (size_t i = 0; i < consumed; ++i) {
      wordStore.release(storedWordAt(i));  // retires arena chunks as lines are consumed
    }
    xp_tokens_erase_prefix(tokens(), consumed);
    if (!rubyTexts.empty()) {
      const size_t rtConsumed = std::min(consumed, rubyTexts.size());
      rubyTexts.erase(rubyTexts.begin(), rubyTexts.begin() + rtConsumed);
    }
  }
}

static inline bool isCjkIdeograph(uint32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x3FFFF);
}

// The first word of a line may have its ruby characters wider than the word (the base text). In that case, we need to
// move the base text to the right a bit so that ruby text doesn't overflow the left border, and it is still centered
// over the base text. This function calculates how much we need to move the base text to the right.
int ParsedText::calculateRubyExtraStartOffset(const size_t wordIdx, const size_t maxWordIdx,
                                              const GfxRenderer& renderer, const int fontId) const {
  if (rubyTexts.empty() || wordIdx >= rubyTexts.size() || rubyTexts[wordIdx].empty() ||
      (storedStyleAt(wordIdx) & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }

  size_t groupWordCount = 1;
  while (wordIdx + groupWordCount < maxWordIdx &&
         (storedStyleAt(wordIdx + groupWordCount) & EpdFontFamily::RUBY_CONTINUE) != 0) {
    groupWordCount++;
  }
  int groupActualWidth = 0;
  for (size_t k = 0; k < groupWordCount; ++k) {
    groupActualWidth += measureWordWidth(renderer, fontId, wordAt(wordIdx + k), getWordStyleAt(wordIdx + k),
                                         blockStyle.characterSpacing, wordSpacingPercent);
  }
  const int rubyWidth =
      renderer.getTextAdvanceX(fontId, rubyTexts[wordIdx].c_str(), EpdFontFamily::SUP, blockStyle.characterSpacing);
  if (rubyWidth <= groupActualWidth) {
    return 0;
  }

  const int leftOverlap = (rubyWidth - groupActualWidth) / 2;

  // This function is only ever called for the first word of a line.
  // storedWordAt(wordIdx - 1), if it exists, is always the last word of the *prior* line
  // and cannot absorb any left overhang on the current line.
  // The full leftOverlap must therefore be reserved as a visual indent so the
  // ruby text does not overflow the left margin.
  return leftOverlap;
}

// The last ruby group on a line may have its ruby characters wider than the group's base text.
// The right half of that overhang protrudes past the last base character. This function returns
// the amount of right-margin space that must be reserved so the ruby does not overflow the right
// border. It mirrors calculateRubyExtraStartOffset: storedWordAt(lineBreak) is on the *next* line and
// cannot absorb any of the right overhang on the current line, so the full rightOverlap is returned.
int ParsedText::calculateRubyExtraEndOffset(const size_t lineStartIdx, const size_t lineBreakIdx,
                                            const GfxRenderer& renderer, const int fontId) const {
  if (rubyTexts.empty() || lineBreakIdx == 0 || lineStartIdx >= lineBreakIdx) {
    return 0;
  }

  // Walk backwards from the last word to find the leader of the last ruby group on the line.
  size_t leaderIdx = lineBreakIdx - 1;
  while (leaderIdx > lineStartIdx && (storedStyleAt(leaderIdx) & EpdFontFamily::RUBY_CONTINUE) != 0) {
    leaderIdx--;
  }

  // leaderIdx must be a ruby group leader (non-empty ruby, no RUBY_CONTINUE flag).
  if (leaderIdx >= rubyTexts.size() || rubyTexts[leaderIdx].empty() ||
      (storedStyleAt(leaderIdx) & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }

  // Measure the group.
  int groupActualWidth = 0;
  for (size_t k = leaderIdx; k < lineBreakIdx; ++k) {
    groupActualWidth += measureWordWidth(renderer, fontId, wordAt(k), getWordStyleAt(k), blockStyle.characterSpacing,
                                         wordSpacingPercent);
  }
  const int rubyWidth =
      renderer.getTextAdvanceX(fontId, rubyTexts[leaderIdx].c_str(), EpdFontFamily::SUP, blockStyle.characterSpacing);
  if (rubyWidth <= groupActualWidth) {
    return 0;
  }

  return (rubyWidth - groupActualWidth) / 2;
}

int ParsedText::wordFontId(const size_t wordIndex, const int blockFontId) const {
  const uint8_t slot = tokenAt(wordIndex).slot & ~TextBlock::SMALL_CAPS_SLOT_FLAG;
  return slot == 0 || sizeSlotFontIds[slot] == 0 ? blockFontId : sizeSlotFontIds[slot];
}

uint8_t ParsedText::sizeSlotFor(const float scale) {
  uint8_t nearest = 1;
  for (uint8_t slot = 1; slot < sizeSlotCount; ++slot) {
    if (std::fabs(sizeSlotScales[slot] - scale) < 0.01f) return slot;
    if (std::fabs(sizeSlotScales[slot] - scale) < std::fabs(sizeSlotScales[nearest] - scale)) nearest = slot;
  }
  if (sizeSlotCount < MAX_SIZE_SLOTS) {
    sizeSlotScales[sizeSlotCount] = scale;
    return sizeSlotCount++;
  }
  return nearest;
}

ParagraphWidths ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  ParagraphWidths wordWidths;
  wordWidths.reserve(tokenCount());
#if XP_RUST_LAYOUT
  if (!wordWidths.valid()) return wordWidths;
#endif

  for (size_t i = 0; i < tokenCount(); ++i) {
    wordWidths.push_back(measureFocusWordWidth(renderer, wordFontId(i, fontId), wordAt(i), getWordStyleAt(i),
                                               tokenAt(i).focus, blockStyle.characterSpacing, wordSpacingPercent));
  }

  // Adjust widths for ruby groups to comply with JLReq standards
  if (!rubyTexts.empty()) {
    struct RubyGroupInfo {
      size_t start;
      size_t count;
      int baseWidth;
      int rubyWidth;
      int leftOverlap;
      int rightOverlap;
    };

    std::vector<RubyGroupInfo> groups;
    for (size_t i = 0; i < tokenCount(); ++i) {
      if (i < rubyTexts.size() && !rubyTexts[i].empty() && (storedStyleAt(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
        RubyGroupInfo g;
        g.start = i;
        g.baseWidth = wordWidths[i];
        g.count = 1;
        while (i + g.count < tokenCount() && (storedStyleAt(i + g.count) & EpdFontFamily::RUBY_CONTINUE) != 0) {
          g.baseWidth += wordWidths[i + g.count];
          g.count++;
        }
        g.rubyWidth =
            renderer.getTextAdvanceX(fontId, rubyTexts[i].c_str(), EpdFontFamily::SUP, blockStyle.characterSpacing);
        g.leftOverlap = std::max(0, (g.rubyWidth - g.baseWidth) / 2);
        g.rightOverlap = std::max(0, (g.rubyWidth - g.baseWidth) / 2);
        groups.push_back(g);
        i += g.count - 1;
      }
    }

    // Adjust widths based on adjacent characters and group-to-group spacing
    for (size_t gIdx = 0; gIdx < groups.size(); ++gIdx) {
      const auto& g = groups[gIdx];

      // 1. Preceding character (left overhang)
      if (g.start > 0) {
        const uint32_t cpPrev = lastCodepoint(wordAt(g.start - 1));
        if (isCjkIdeograph(cpPrev)) {
          wordWidths[g.start - 1] += g.leftOverlap;
        } else {
          const int maxLeftOverhang = wordWidths[g.start - 1] / 2;
          wordWidths[g.start - 1] += std::max(0, g.leftOverlap - maxLeftOverhang);
        }
      }

      // 2. Succeeding character (right overhang / group collision)
      const size_t nextIdx = g.start + g.count;
      if (nextIdx < tokenCount()) {
        if (gIdx + 1 < groups.size() && groups[gIdx + 1].start == nextIdx) {
          // Adjacent ruby groups: compute collision
          const auto& nextG = groups[gIdx + 1];
          const int collision = g.rightOverlap + nextG.leftOverlap;
          if (collision > 0) {
            wordWidths[g.start + g.count - 1] += collision;
          }
        } else {
          // Regular character following: check if it's Kanji
          const uint32_t cpNext = firstCodepoint(wordAt(nextIdx));
          if (isCjkIdeograph(cpNext)) {
            wordWidths[g.start + g.count - 1] += g.rightOverlap;
          } else {
            const int maxRightOverhang = wordWidths[nextIdx] / 2;
            wordWidths[g.start + g.count - 1] += std::max(0, g.rightOverlap - maxRightOverhang);
          }

          // Check if there is another ruby group further ahead separated only by non-ideographs
          if (gIdx + 1 < groups.size()) {
            const auto& nextG = groups[gIdx + 1];
            bool onlyNonIdeographsInBetween = true;
            int gapWidth = 0;
            for (size_t k = nextIdx; k < nextG.start; ++k) {
              const uint32_t cp = firstCodepoint(wordAt(k));
              if (isCjkIdeograph(cp)) {
                onlyNonIdeographsInBetween = false;
                break;
              }
              gapWidth += wordWidths[k];
            }
            if (onlyNonIdeographsInBetween) {
              const int maxRightOverhang = wordWidths[g.start + g.count - 1] / 2;
              const int maxLeftOverhang = wordWidths[nextG.start - 1] / 2;
              const int allowedRight = std::min(g.rightOverlap, maxRightOverhang);
              const int allowedLeft = std::min(nextG.leftOverlap, maxLeftOverhang);
              const int touchOverlap = allowedRight + allowedLeft - gapWidth;
              if (touchOverlap > 0) {
                wordWidths[g.start + g.count - 1] += touchOverlap;
              }
            }
          }
        }
      }
    }
  }

  return wordWidths;
}

#if XP_RUST_LAYOUT
ParagraphBreaks ParsedText::computeRustLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  ParagraphWidths& wordWidths, const size_t maxLines) {
  ParagraphBreaks breaks;
  struct Context {
    ParsedText& paragraph;
    const GfxRenderer& renderer;
    int fontId;
    ParagraphWidths& widths;
    ParagraphBreaks& breaks;
  } context{*this, renderer, fontId, wordWidths, breaks};
  const XpLayoutCallbacks callbacks = {
      &context,
      &wordWidths.state,
      static_cast<uint8_t>(!rubyTexts.empty()),
      [](void* raw, size_t i) -> uint8_t {
        const auto& p = static_cast<Context*>(raw)->paragraph;
        return TokenBoundary::allowsBreak(p.tokenContinues(i), p.tokenNoSpaceBefore(i));
      },
      [](void* raw, size_t i) -> int32_t {
        const auto& c = *static_cast<Context*>(raw);
        const auto& p = c.paragraph;
        if (p.tokenContinues(i)) {
          return c.renderer.getKerning(c.fontId, lastCodepoint(p.wordAt(i - 1)), firstCodepoint(p.wordAt(i)),
                                       p.getWordStyleAt(i - 1), p.blockStyle.characterSpacing);
        }
        if (p.tokenNoSpaceBefore(i)) return p.blockStyle.characterSpacing;
        return scaleSpace(c.renderer.getSpaceAdvance(c.fontId, lastCodepoint(p.wordAt(i - 1)),
                                                     firstCodepoint(p.wordAt(i)), p.getWordStyleAt(i - 1)),
                          p.wordSpacingPercent);
      },
      [](void* raw, size_t start, size_t end) -> int32_t {
        const auto& c = *static_cast<Context*>(raw);
        return c.paragraph.calculateRubyExtraStartOffset(start, end, c.renderer, c.fontId);
      },
      [](void* raw, size_t start, size_t end) -> int32_t {
        const auto& c = *static_cast<Context*>(raw);
        return c.paragraph.calculateRubyExtraEndOffset(start, end, c.renderer, c.fontId);
      },
      [](void* raw, size_t i, int32_t available, uint8_t fallback) -> uint8_t {
        auto& c = *static_cast<Context*>(raw);
        return c.paragraph.hyphenateWordAtIndex(i, available, c.renderer, c.fontId, c.widths, fallback != 0);
      },
      [](void* raw, size_t end) -> uint8_t { return static_cast<Context*>(raw)->breaks.push_back(end); }};
  const int indent = resolveFirstLineIndent(true, renderer, fontId);
  const bool ok = hyphenationEnabled || maxLines != SIZE_MAX
                      ? xp_layout_greedy(&callbacks, pageWidth, indent, maxLines, firstLineConsumed, hyphenationEnabled)
                      : xp_layout_optimal(&callbacks, paragraphAllocator(), pageWidth, indent);
  if (!ok) breaks.clear();
  return breaks;
}
#endif

ParagraphBreaks ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                              ParagraphWidths& wordWidths) {
  if ((tokenCount() == 0)) {
    return {};
  }

  const int firstLineIndent = resolveFirstLineIndent(true, renderer, fontId);

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    // First word needs to fit in reduced width if there's an indent
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = tokenCount();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a continuation
      int gap = 0;
      if (j > static_cast<size_t>(i) && tokenContinues(j)) {
        // Attached and breakable-attached boundaries both use kerning when kept on one line.
        gap = renderer.getKerning(fontId, lastCodepoint(wordAt(j - 1)), firstCodepoint(wordAt(j)),
                                  getWordStyleAt(j - 1), blockStyle.characterSpacing);
      } else if (j > static_cast<size_t>(i) && tokenNoSpaceBefore(j)) {
        gap = blockStyle.characterSpacing;
      } else if (j > static_cast<size_t>(i)) {
        gap = scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(wordAt(j - 1)), firstCodepoint(wordAt(j)),
                                                  getWordStyleAt(j - 1)),
                         wordSpacingPercent);
      }

      // Calculate extraStartOffset for the first word on the line (i) (protect left margin)
      const int extraStartOffset = (j == i) ? calculateRubyExtraStartOffset(i, totalWordCount, renderer, fontId) : 0;

      currlen += wordWidths[j] + gap + (j == i ? extraStartOffset : 0);

      if (currlen > effectivePageWidth) {
        break;
      }

      // A normal continuation is unbreakable. continues=true + noSpaceBefore=true is the compact
      // breakable-attachment state used after explicit hyphen characters.
      if (j + 1 < totalWordCount && !TokenBoundary::allowsBreak(tokenContinues(j + 1), tokenNoSpaceBefore(j + 1))) {
        continue;
      }

      const int extraEndOffset = calculateRubyExtraEndOffset(i, j + 1, renderer, fontId);

      if (currlen + extraEndOffset > effectivePageWidth) {
        continue;  // Cannot split here as it would overflow the right margin
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      // Favor longer lines when line-breaking costs are equal, to avoid unnecessary short lines in Chinese and Japanese
      // text.
      if (cost <= dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  ParagraphBreaks lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
ParagraphBreaks ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                        const int pageWidth, ParagraphWidths& wordWidths,
                                                        const size_t maxLines) {
  const int firstLineIndent = resolveFirstLineIndent(true, renderer, fontId);

  ParagraphBreaks lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = !firstLineConsumed;

  while (currentIndex < wordWidths.size() && lineBreakIndices.size() < maxLines) {
    const size_t lineStart = currentIndex;
    size_t lastFittingBreak = lineStart;
    int lineWidth = calculateRubyExtraStartOffset(lineStart, wordWidths.size(), renderer, fontId);

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      int spacing = 0;
      if (!isFirstWord && tokenContinues(currentIndex)) {
        // Attached and breakable-attached boundaries both use kerning when kept on one line.
        spacing =
            renderer.getKerning(fontId, lastCodepoint(wordAt(currentIndex - 1)), firstCodepoint(wordAt(currentIndex)),
                                getWordStyleAt(currentIndex - 1), blockStyle.characterSpacing);
      } else if (!isFirstWord && tokenNoSpaceBefore(currentIndex)) {
        spacing = blockStyle.characterSpacing;
      } else if (!isFirstWord) {
        spacing =
            scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(wordAt(currentIndex - 1)),
                                                firstCodepoint(wordAt(currentIndex)), getWordStyleAt(currentIndex - 1)),
                       wordSpacingPercent);
      }
      const int candidateWidth = spacing + wordWidths[currentIndex];

      // Word fits on current line
      const int extraEndOffset = calculateRubyExtraEndOffset(lineStart, currentIndex + 1, renderer, fontId);
      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        if (lineWidth + extraEndOffset <= effectivePageWidth &&
            (currentIndex == wordWidths.size() ||
             TokenBoundary::allowsBreak(tokenContinues(currentIndex), tokenNoSpaceBefore(currentIndex)))) {
          lastFittingBreak = currentIndex;
        }
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (lineWidth + candidateWidth > effectivePageWidth && availableWidth > 0 &&
          (hyphenationEnabled || isFirstWord) &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        ++currentIndex;
        lastFittingBreak = currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        ++currentIndex;
      }
      break;
    }

    if (lastFittingBreak > lineStart) currentIndex = lastFittingBreak;

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() &&
           !TokenBoundary::allowsBreak(tokenContinues(currentIndex), tokenNoSpaceBefore(currentIndex))) {
      --currentIndex;
    }

    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

// Splits storedWordAt(wordIndex) into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint
// fits the available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, ParagraphWidths& wordWidths, const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= tokenCount()) {
    return false;
  }

#if XP_RUST_LAYOUT
  struct Context {
    ParsedText& paragraph;
    const GfxRenderer& renderer;
    int font;
  } context{*this, renderer, wordFontId(wordIndex, fontId)};
  const XpLayoutSplitRequest request = {
      &context,
      [](void* raw, const uint8_t* data, size_t len, uint8_t style, uint8_t focus, uint8_t hyphen) -> uint16_t {
        const auto& c = *static_cast<Context*>(raw);
        return measureFocusWordWidth(
            c.renderer, c.font, {reinterpret_cast<const char*>(data), len}, static_cast<EpdFontFamily::Style>(style),
            focus, c.paragraph.blockStyle.characterSpacing, c.paragraph.wordSpacingPercent, hyphen != 0);
      },
      [](uint32_t previous, uint32_t current) -> uint8_t { return CjkBreaks::boundaryAllowsBreak(previous, current); },
      [](void* raw, size_t index) {
        auto& ruby = static_cast<Context*>(raw)->paragraph.rubyTexts;
        if (index <= ruby.size()) ruby.insert(ruby.begin() + index, "");
      },
      wordIndex,
      availableWidth,
      Hyphenator::preferredLanguageId(),
      static_cast<uint8_t>(getWordStyleAt(wordIndex)),
      static_cast<uint8_t>(allowFallbackBreaks)};
  XpLayoutSplitResult split{};
  const bool splitSucceeded = xp_layout_split(wordStore.handle(), tokens(), &wordWidths.state, &request, &split);
  return splitSucceeded;
#else
  // Stable copy: Hyphenator and the prefix measurements below want std::string
  // semantics. Bounded by one word.
  const std::string word{wordAt(wordIndex)};  // brace-init: `word(` is an Arduino macro
  const auto style = getWordStyleAt(wordIndex);

  const uint8_t focusBoundary = tokenAt(wordIndex).focus;

  // Collect candidate breakpoints (byte offsets and hyphen requirements). Focus emphasis is a byte
  // annotation, so the hyphenator sees the whole word and every legal break is reachable.
  // Hangul breaks come first so they win a tie against a hyphenated break at the same width.
  auto breakInfos = hangulLineEndBreaks(word);
  const auto hyphenBreaks = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  breakInfos.insert(breakInfos.end(), hyphenBreaks.begin(), hyphenBreaks.end());
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureFocusWordWidth(renderer, wordFontId(wordIndex, fontId), word.substr(0, offset),
                                                  style, focusBoundaryBefore(focusBoundary, offset),
                                                  blockStyle.characterSpacing, wordSpacingPercent, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  uint32_t prefixLength = 0;
  const unsigned char* offsetPtr = reinterpret_cast<const unsigned char*>(word.data());
  const unsigned char* splitPtr = offsetPtr + chosenOffset;
  while (offsetPtr < splitPtr) {
    utf8NextCodepoint(&offsetPtr);
    prefixLength++;
  }
  const uint32_t remainderOffset = sourceOffsetAfter(visibleOffsetAt(wordIndex), prefixLength);

  // Split the word at the selected breakpoint. The prefix is materialized as a
  // fresh arena entry (with its visible hyphen, so it stays NUL-terminated);
  // the remainder aliases the original word's tail bytes and inherits the
  // original entry's release obligation via WordStore::suffix().
  // Stack buffer, not std::string: this runs mid-pagination, exactly when the
  // heap is under section-build pressure. The parser caps words at
  // MAX_WORD_SIZE (200) bytes; anything larger skips the split rather than
  // overflow (the word then breaks whole, as when no breakpoint fits).
  char prefixBuf[208];
  if (chosenOffset + 1 > sizeof(prefixBuf)) {
    return false;
  }
  memcpy(prefixBuf, word.data(), chosenOffset);
  size_t prefixLen = chosenOffset;
  if (chosenNeedsHyphen) {
    prefixBuf[prefixLen++] = '-';
  }
#if XP_RUST_LAYOUT
  wordWidths.reserve(wordWidths.size() + 1);
  if (!wordWidths.valid()) return false;
#endif
  WordStore::StoredWord prefixStored;
  if (!wordStore.append(prefixBuf, prefixLen, prefixStored)) {
    // OOM: skip the split; the word stays whole and the line breaks without it.
    return false;
  }
  const WordStore::StoredWord remainderStored = WordStore::suffix(storedWordAt(wordIndex), chosenOffset);
  auto prefix = tokenAt(wordIndex);
  auto remainder = prefix;
  const auto storedStyle = static_cast<uint8_t>(style & ~EpdFontFamily::SMALL_CAPS);
  remainder.word = {remainderStored.chunk, remainderStored.off, remainderStored.len};
  remainder.style = storedStyle;
  remainder.focus = focusBoundaryAfter(focusBoundary, chosenOffset);
  remainder.flags = 0;
  if (!xp_tokens_insert(tokens(), wordIndex + 1, &remainder, remainderOffset)) {
    wordStore.release(prefixStored);
    LOG_ERR("PTX", "OOM: split token metadata");
    return false;
  }
  prefix.word = {prefixStored.chunk, prefixStored.off, prefixStored.len};
  prefix.style = storedStyle | (chosenNeedsHyphen ? TextBlock::DISCRETIONARY_HYPHEN_FLAG : 0);
  prefix.focus = focusBoundaryBefore(focusBoundary, chosenOffset);
  // An all-bold prefix carries BOLD instead of an emphasis boundary at its end.
  if (prefix.focus + (chosenNeedsHyphen ? 1u : 0u) >= prefix.word.len) {
    prefix.style |= EpdFontFamily::BOLD;
    prefix.focus = 0;
  }
  xp_tokens_set(tokens(), wordIndex, &prefix);
  if (wordIndex + 1 <= rubyTexts.size()) {
    rubyTexts.insert(rubyTexts.begin() + wordIndex + 1, "");
  }

  // The prefix retains its attachment flags; the remainder starts a new line.

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth =
      measureFocusWordWidth(renderer, wordFontId(wordIndex, fontId), wordStore.view(remainderStored), style,
                            tokenAt(wordIndex + 1).focus, blockStyle.characterSpacing, wordSpacingPercent);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
#endif
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const ParagraphWidths& wordWidths,
                             const ParagraphBreaks& lineBreakIndices,
                             const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                             const GfxRenderer& renderer, const int fontId) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  // A no-break space carried across a soft wrap must not indent the next line.
  if (breakIndex > 0 || paragraphStartEmitted) {
    while (lastBreakAt + 1 < lineBreak && tokenContinues(lastBreakAt) && wordAt(lastBreakAt) == " ") {
      ++lastBreakAt;
    }
  }
  const size_t lineWordCount = lineBreak - lastBreakAt;
  const uint32_t lineVisibleOffset = visibleOffsetAt(lastBreakAt);
#if XP_RUST_LAYOUT
  bool hasFocus = false;
  for (size_t i = lastBreakAt; i < lineBreak; ++i) hasFocus |= tokenAt(i).focus != 0;
  ParagraphLine extracted(lineWordCount, hasFocus);
  if (!extracted.valid()) {
    LOG_ERR("PTX", "OOM: line metadata (%zu words)", lineWordCount);
    droppedWords = true;
    return;
  }
#endif

  const int firstLineIndent = resolveFirstLineIndent(breakIndex == 0, renderer, fontId);

  std::vector<std::string> lineRubyTexts(lineWordCount);
  if (!rubyTexts.empty() && lastBreakAt < rubyTexts.size()) {
    const size_t copyCount = std::min(lineBreak, rubyTexts.size()) - lastBreakAt;
    std::copy(rubyTexts.begin() + lastBreakAt, rubyTexts.begin() + lastBreakAt + copyCount, lineRubyTexts.begin());
  }

  const int extraStartOffset = calculateRubyExtraStartOffset(lastBreakAt, lineBreak, renderer, fontId);
  const int extraEndOffset = calculateRubyExtraEndOffset(lastBreakAt, lineBreak, renderer, fontId);

  std::vector<std::string> lineWords;
  lineWords.reserve(lineWordCount);
#if XP_RUST_LAYOUT
  auto lineWordStyles = extracted.styles();
#else
  std::vector<EpdFontFamily::Style> lineWordStyles;
  lineWordStyles.reserve(lineWordCount);
#endif

  for (size_t i = 0; i < lineWordCount; ++i) {
    // Copy out of the arena: TextBlock construction and bidi reorder below
    // want owning strings. Bounded by one line; the arena bytes are released
    // by the consumed-prefix pass in layoutAndExtractLines.
    std::string word{wordAt(lastBreakAt + i)};  // brace-init: `word(` is an Arduino macro
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
    lineWords.push_back(std::move(word));
    // TextBlock keeps small caps with its word fonts (see attachWordFonts).
#if XP_RUST_LAYOUT
    lineWordStyles[i] = static_cast<EpdFontFamily::Style>(getWordStyleAt(lastBreakAt + i) & ~EpdFontFamily::SMALL_CAPS);
#else
    lineWordStyles.push_back(
        static_cast<EpdFontFamily::Style>(getWordStyleAt(lastBreakAt + i) & ~EpdFontFamily::SMALL_CAPS));
#endif
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  [[maybe_unused]] const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = lineBreak == tokenCount();

  // For RTL, implicit/default Left alignment becomes Right alignment.
  // Explicit text-align:left must remain left for CSS correctness.
  const CssTextAlign effectiveAlignment =
      (blockStyle.isRtl && !blockStyle.textAlignDefined && blockStyle.alignment == CssTextAlign::Left)
          ? CssTextAlign::Right
          : blockStyle.alignment;

#if XP_RUST_LAYOUT
  struct PositionContext {
    const ParsedText& paragraph;
    const GfxRenderer& renderer;
    const ParagraphWidths& widths;
    const std::vector<std::string>& words;
    std::span<const EpdFontFamily::Style> styles;
    size_t start;
    int font;
    bool reordered;
  } positionContext{*this, renderer, wordWidths, lineWords, lineWordStyles, lastBreakAt, fontId, false};
  const XpLineCallbacks lineCallbacks = {
      &positionContext,
      [](void* raw, size_t i) -> XpLineWordMetrics {
        const auto& c = *static_cast<PositionContext*>(raw);
        const auto& p = c.paragraph;
        if (c.reordered) {
          return {p.reorderedWidthsScratch[i], static_cast<uint8_t>(p.reorderedContinuesScratch[i]),
                  static_cast<uint8_t>(p.reorderedWordsScratch[i] == " ")};
        }
        return {c.widths[c.start + i], static_cast<uint8_t>(p.tokenContinues(c.start + i)),
                static_cast<uint8_t>(c.words[i] == " ")};
      },
      [](void* raw, size_t i) -> int32_t {
        const auto& c = *static_cast<PositionContext*>(raw);
        const auto& p = c.paragraph;
        const auto& words = c.reordered ? p.reorderedWordsScratch : c.words;
        const std::span<const EpdFontFamily::Style> styles =
            c.reordered ? std::span<const EpdFontFamily::Style>(p.reorderedStylesScratch) : c.styles;
        const bool continues = c.reordered ? p.reorderedContinuesScratch[i] : p.tokenContinues(c.start + i);
        const bool noSpace = c.reordered ? p.reorderedNoSpaceBeforeScratch[i] : p.tokenNoSpaceBefore(c.start + i);
        if (continues) {
          return c.renderer.getKerning(c.font, lastCodepoint(words[i - 1]), firstCodepoint(words[i]), styles[i - 1],
                                       p.blockStyle.characterSpacing);
        }
        if (noSpace) return p.blockStyle.characterSpacing;
        return scaleSpace(
            c.renderer.getSpaceAdvance(c.font, lastCodepoint(words[i - 1]), firstCodepoint(words[i]), styles[i - 1]),
            p.wordSpacingPercent);
      }};
  XpLineSpec lineSpec{lineWordCount,
                      pageWidth,
                      firstLineIndent,
                      extraStartOffset,
                      extraEndOffset,
                      static_cast<uint8_t>(effectiveAlignment),
                      static_cast<uint8_t>(blockStyle.isRtl),
                      0,
                      static_cast<uint8_t>(isLastLine)};
  XpLineMetrics logicalMetrics{};
  xp_layout_line_metrics(&lineSpec, &lineCallbacks, &logicalMetrics);
  [[maybe_unused]] const int lineWordWidthSum = logicalMetrics.width;
  [[maybe_unused]] const size_t actualGapCount = logicalMetrics.gap_count;
  [[maybe_unused]] const int totalNaturalGaps = logicalMetrics.natural_gaps;
  [[maybe_unused]] const int spareSpace = logicalMetrics.spare;
  [[maybe_unused]] const int justifyExtra = logicalMetrics.justify;
#else
  // Calculate total word width for this line, count actual word gaps,
  // and accumulate total natural gap widths (including space kerning adjustments).
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    if (wordIdx == 0) continue;
    const size_t boundaryIdx = lastBreakAt + wordIdx;
    const bool isStretchableSpace =
        lineWords[wordIdx] == " " && wordIdx + 1 < lineWordCount && tokenContinues(boundaryIdx + 1);
    if (TokenBoundary::isJustifiableGap(tokenContinues(boundaryIdx), isStretchableSpace)) {
      actualGapCount++;
    }
    if (tokenContinues(boundaryIdx)) {
      totalNaturalGaps +=
          renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx - 1]), firstCodepoint(lineWords[wordIdx]),
                              lineWordStyles[wordIdx - 1], blockStyle.characterSpacing);
    } else if (tokenNoSpaceBefore(boundaryIdx)) {
      totalNaturalGaps += blockStyle.characterSpacing;
    } else {
      totalNaturalGaps +=
          scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx - 1]),
                                              firstCodepoint(lineWords[wordIdx]), lineWordStyles[wordIdx - 1]),
                     wordSpacingPercent);
    }
  }

  // For justified text, compute per-gap extra to distribute remaining space evenly.
  // extraEndOffset reserves space for any ruby group at the right edge of the line.
  const int spareSpace = effectivePageWidth - extraStartOffset - extraEndOffset - lineWordWidthSum - totalNaturalGaps;
  const int justifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                               ? computeJustifyExtra(spareSpace, actualGapCount)
                               : 0;

#endif

  // BiDi processing: reorder words with UAX#9 in full-line context.
  visualOrderScratch.clear();
  visualOrderScratch.reserve(lineWordCount);
  // Skip expensive visual-order resolution for pure LTR paragraphs that have no RTL words.
  const bool shouldResolveVisualOrder = blockStyle.isRtl || hasRtlWord;
  const bool willReorder =
      shouldResolveVisualOrder && BidiUtils::computeVisualWordOrder(lineWords, blockStyle.isRtl, visualOrderScratch);

#if XP_RUST_LAYOUT
  auto lineXPos = extracted.positions();
#else
  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);
#endif

  if (willReorder) {
    reorderedWordsScratch.clear();
    reorderedStylesScratch.clear();
    reorderedWidthsScratch.clear();
    reorderedContinuesScratch.clear();
    reorderedNoSpaceBeforeScratch.clear();
    reorderedFocusBoundaryScratch.clear();
    reorderedWordsScratch.reserve(visualOrderScratch.size());
    reorderedStylesScratch.reserve(visualOrderScratch.size());
    reorderedWidthsScratch.reserve(visualOrderScratch.size());
    reorderedContinuesScratch.reserve(visualOrderScratch.size());
    reorderedNoSpaceBeforeScratch.reserve(visualOrderScratch.size());
    reorderedFocusBoundaryScratch.reserve(visualOrderScratch.size());

    for (size_t i = 0; i < visualOrderScratch.size(); ++i) {
      const uint16_t src = visualOrderScratch[i];
      reorderedWordsScratch.push_back(std::move(lineWords[src]));
      reorderedStylesScratch.push_back(lineWordStyles[src]);
      reorderedWidthsScratch.push_back(wordWidths[lastBreakAt + src]);
      reorderedFocusBoundaryScratch.push_back(tokenAt(lastBreakAt + src).focus);

      // Continuation means "no break/gap between two adjacent logical tokens".
      // After visual reordering (common in RTL), an adjacent logical pair can appear
      // as either (prev -> curr) or (curr -> prev) in visual order; preserve both.
      bool continues = false;
      if (i > 0) {
        const size_t prevSrc = visualOrderScratch[i - 1];
        const size_t currSrc = src;
        const bool forwardAdjacent = currSrc == prevSrc + 1;
        const bool reverseAdjacent = prevSrc == currSrc + 1;

        if (forwardAdjacent && tokenContinues(lastBreakAt + currSrc)) {
          continues = true;
        } else if (reverseAdjacent && tokenContinues(lastBreakAt + prevSrc)) {
          continues = true;
        }
      }
      reorderedContinuesScratch.push_back(continues);
      reorderedNoSpaceBeforeScratch.push_back(!continues && tokenNoSpaceBefore(lastBreakAt + src));
    }

#if XP_RUST_LAYOUT
    positionContext.reordered = true;
    lineSpec.reordered = 1;
    XpLineMetrics visualMetrics{};
    xp_layout_line_metrics(&lineSpec, &lineCallbacks, &visualMetrics);
    // Rust visual placement.
    xp_layout_line_position(&lineSpec, &lineCallbacks, &visualMetrics, lineXPos.data());
#else
    int reorderedWordWidthSum = 0;
    size_t reorderedGapCount = 0;
    int reorderedNaturalGaps = 0;
    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); wordIdx++) {
      reorderedWordWidthSum += reorderedWidthsScratch[wordIdx];
      if (wordIdx > 0 && reorderedNoSpaceBeforeScratch[wordIdx]) {
        // Unicode break opportunity with no inserted Latin-style space. It is still
        // a stretchable gap for justified CJK/Korean text.
        reorderedGapCount++;
        reorderedNaturalGaps += blockStyle.characterSpacing;
      } else if (wordIdx > 0 && !reorderedContinuesScratch[wordIdx]) {
        reorderedGapCount++;
        reorderedNaturalGaps +=
            scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                                firstCodepoint(reorderedWordsScratch[wordIdx]),
                                                reorderedStylesScratch[wordIdx - 1]),
                       wordSpacingPercent);
      } else if (wordIdx > 0 && reorderedContinuesScratch[wordIdx]) {
        if (reorderedWordsScratch[wordIdx] == " " && wordIdx + 1 < reorderedWidthsScratch.size() &&
            reorderedContinuesScratch[wordIdx + 1]) {
          reorderedGapCount++;
        }
        reorderedNaturalGaps += renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                                    firstCodepoint(reorderedWordsScratch[wordIdx]),
                                                    reorderedStylesScratch[wordIdx - 1], blockStyle.characterSpacing);
      }
    }

    const int reorderedSpare =
        effectivePageWidth - extraStartOffset - extraEndOffset - reorderedWordWidthSum - reorderedNaturalGaps;
    const int reorderedJustifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                          ? computeJustifyExtra(reorderedSpare, reorderedGapCount)
                                          : 0;

    const int justifyContribution = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                        ? reorderedJustifyExtra * static_cast<int>(reorderedGapCount)
                                        : 0;
    const int contentWidth = reorderedWordWidthSum + reorderedNaturalGaps + justifyContribution;

    int xpos = 0;
    if (blockStyle.isRtl) {
      if (effectiveAlignment == CssTextAlign::Right || effectiveAlignment == CssTextAlign::Justify) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    } else {
      xpos = firstLineIndent;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    }

    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); wordIdx++) {
      lineXPos.push_back(static_cast<int16_t>(xpos));
      xpos += reorderedWidthsScratch[wordIdx];

      const bool nextIsContinuation =
          wordIdx + 1 < reorderedWidthsScratch.size() && reorderedContinuesScratch[wordIdx + 1];
      if (nextIsContinuation) {
        int advance = renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                          firstCodepoint(reorderedWordsScratch[wordIdx + 1]),
                                          reorderedStylesScratch[wordIdx], blockStyle.characterSpacing);
        // wordIdx > 0 mirrors the gap accounting above (which skips index 0): a leading
        // no-break space must not receive justifyExtra, or the line over-stretches by one
        // gap and the last word is pushed past the right margin (issue #2185).
        if (wordIdx > 0 && reorderedWordsScratch[wordIdx] == " " && reorderedContinuesScratch[wordIdx] &&
            effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
          advance += reorderedJustifyExtra;
        }
        xpos += advance;
      } else if (wordIdx + 1 < reorderedWidthsScratch.size()) {
        const bool nextNoSpace = reorderedNoSpaceBeforeScratch[wordIdx + 1];
        int gap = nextNoSpace
                      ? blockStyle.characterSpacing
                      : scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                                            firstCodepoint(reorderedWordsScratch[wordIdx + 1]),
                                                            reorderedStylesScratch[wordIdx]),
                                   wordSpacingPercent);
        if (effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
          gap += reorderedJustifyExtra;
        }
        xpos += gap;
      }
    }

#endif

    lineWords.swap(reorderedWordsScratch);
#if XP_RUST_LAYOUT
    std::copy(reorderedStylesScratch.begin(), reorderedStylesScratch.end(), lineWordStyles.begin());
#else
    lineWordStyles.swap(reorderedStylesScratch);
#endif
  } else {
#if XP_RUST_LAYOUT
    xp_layout_line_position(&lineSpec, &lineCallbacks, &logicalMetrics, lineXPos.data());
#else
    // Standard LTR/RTL positioning loop when no visual reordering is needed
    if (blockStyle.isRtl) {
      // RTL: position words from right to left
      int xpos = effectivePageWidth;
      if (effectiveAlignment == CssTextAlign::Left) {
        // Explicit left alignment in RTL context
        xpos = lineWordWidthSum + totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth + lineWordWidthSum + totalNaturalGaps) / 2;
      }
      // For Right and Justify, start from right edge (xpos = effectivePageWidth)

      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        xpos -= wordWidths[lastBreakAt + wordIdx];
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && tokenContinues(lastBreakAt + wordIdx + 1);
        if (nextIsContinuation) {
          // Cross-boundary kerning for continuation words
          int advance =
              renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]), firstCodepoint(lineWords[wordIdx + 1]),
                                  lineWordStyles[wordIdx], blockStyle.characterSpacing);
          // wordIdx > 0: see the LTR branch — a leading no-break space is not a justifiable gap.
          if (wordIdx > 0 && lineWords[wordIdx] == " " && tokenContinues(lastBreakAt + wordIdx) &&
              effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            advance += justifyExtra;
          }
          xpos -= advance;
        } else {
          int gap = 0;
          bool nextNoSpace = false;
          if (wordIdx + 1 < lineWordCount) {
            nextNoSpace = tokenNoSpaceBefore(lastBreakAt + wordIdx + 1);
            gap = nextNoSpace ? blockStyle.characterSpacing
                              : scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx]),
                                                                    firstCodepoint(lineWords[wordIdx + 1]),
                                                                    lineWordStyles[wordIdx]),
                                           wordSpacingPercent);
          }
          if (wordIdx + 1 < lineWordCount && effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            gap += justifyExtra;
          }
          xpos -= gap;
        }
      }
    } else {
      // LTR: position words from left to right
      int xpos = firstLineIndent + extraStartOffset;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps) / 2;
      }

      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && tokenContinues(lastBreakAt + wordIdx + 1);
        if (nextIsContinuation) {
          int advance = wordWidths[lastBreakAt + wordIdx];
          advance +=
              renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]), firstCodepoint(lineWords[wordIdx + 1]),
                                  lineWordStyles[wordIdx], blockStyle.characterSpacing);
          // wordIdx > 0 mirrors the gap accounting above (which skips index 0): a leading
          // no-break space must not receive justifyExtra, or the line over-stretches by one
          // gap and the last word is pushed past the right margin (issue #2185).
          if (wordIdx > 0 && lineWords[wordIdx] == " " && tokenContinues(lastBreakAt + wordIdx) &&
              effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            advance += justifyExtra;
          }
          xpos += advance;
        } else {
          int gap = 0;
          bool nextNoSpace = false;
          if (wordIdx + 1 < lineWordCount) {
            nextNoSpace = tokenNoSpaceBefore(lastBreakAt + wordIdx + 1);
            gap = nextNoSpace ? blockStyle.characterSpacing
                              : scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx]),
                                                                    firstCodepoint(lineWords[wordIdx + 1]),
                                                                    lineWordStyles[wordIdx]),
                                           wordSpacingPercent);
          }
          if (wordIdx + 1 < lineWordCount && effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            gap += justifyExtra;
          }
          xpos += wordWidths[lastBreakAt + wordIdx] + gap;
        }
      }
    }
#endif
  }

  const auto focusBoundaryAt = [&](const size_t idx) {
    return willReorder ? reorderedFocusBoundaryScratch[idx] : tokenAt(lastBreakAt + idx).focus;
  };

  std::vector<TextBlock::LinkSpan> lineLinks;
  std::vector<uint8_t> lineLinkIdsSeen;
  for (size_t i = 0; i < lineWordCount; i++) {
    const uint8_t linkId = tokenAt(lastBreakAt + (willReorder ? visualOrderScratch[i] : i)).link;
    if (linkId == 0 || linkId > linkTargets.size()) continue;

    size_t spanIndex = 0;
    while (spanIndex < lineLinkIdsSeen.size() && lineLinkIdsSeen[spanIndex] != linkId) spanIndex++;
    int width = willReorder ? reorderedWidthsScratch[i] : wordWidths[lastBreakAt + i];
    const int right = lineXPos[i] + width;
    const int topLift =
        (lineWordStyles[i] & EpdFontFamily::SUP) != 0 ? renderer.getFontAscenderSize(fontId) * 2 / 5 : 0;

    if (spanIndex == lineLinkIdsSeen.size()) {
      lineLinks.emplace_back();
      auto& span = lineLinks.back();
      strncpy(span.href, linkTargets[linkId - 1].c_str(), sizeof(span.href) - 1);
      span.href[sizeof(span.href) - 1] = '\0';
      span.x = lineXPos[i];
      span.width = static_cast<int16_t>(width);
      span.topLift = static_cast<int16_t>(topLift);
      lineLinkIdsSeen.push_back(linkId);
    } else {
      auto& span = lineLinks[spanIndex];
      const int left = std::min<int>(span.x, lineXPos[i]);
      const int mergedRight = std::max<int>(span.x + span.width, right);
      span.x = static_cast<int16_t>(left);
      span.width = static_cast<int16_t>(mergedRight - left);
      span.topLift = std::max<int16_t>(span.topLift, static_cast<int16_t>(topLift));
    }
  }

  // Lines mixing sizes or using small caps carry per-word fonts; slots resolving to the block
  // font collapse to 0.
  const auto attachWordFonts = [&](TextBlock& block) {
    int32_t fonts[TextBlock::MAX_WORD_FONTS] = {};
    uint8_t slots[128];
    if (lineWordCount > sizeof(slots)) return true;  // pathological line: keep the block font
    bool mixed = false;
    for (size_t i = 0; i < lineWordCount; ++i) {
      const size_t logical = lastBreakAt + (willReorder ? visualOrderScratch[i] : i);
      const int wordFont = wordFontId(logical, fontId);
      uint8_t slot = 0;
      if (wordFont != fontId) {
        for (slot = 1; slot <= TextBlock::MAX_WORD_FONTS; ++slot) {
          if (fonts[slot - 1] == wordFont || fonts[slot - 1] == 0) break;
        }
        if (slot > TextBlock::MAX_WORD_FONTS) slot = 0;
        if (slot > 0) fonts[slot - 1] = wordFont;
      }
      slots[i] = static_cast<uint8_t>(slot | (tokenAt(logical).slot & TextBlock::SMALL_CAPS_SLOT_FLAG));
      mixed = mixed || slots[i] != 0;
    }
    return !mixed || block.setWordFonts(slots, fonts);
  };

  // Fast path: no word on this line carries focus emphasis, so pass empty boundary/suffixX
  // vectors. TextBlock pays zero per-word RAM cost for these annotations when they are empty.
  bool lineHasFocusSplit = false;
  for (size_t i = 0; i < lineWordCount; i++) {
    if (focusBoundaryAt(i) != 0) {
      lineHasFocusSplit = true;
      break;
    }
  }

#if XP_RUST_LAYOUT
  auto sourceRanges = extracted.sources();
  uint16_t paragraphStartWord = UINT16_MAX;
  if (!xp_layout_line_sources(&extracted.state, wordStore.handle(), tokens(), lastBreakAt,
                              willReorder ? visualOrderScratch.data() : nullptr, paragraphStartEmitted,
                              &paragraphStartWord)) {
    LOG_ERR("PTX", "Invalid line source range");
    droppedWords = true;
    return;
  }
#else
  // Preserve source coordinates through visual reordering and discretionary hyphens.
  std::vector<TextBlock::SourceRange> sourceRanges;
  sourceRanges.reserve(lineWordCount);
  uint16_t paragraphStartWord = UINT16_MAX;
  for (size_t i = 0; i < lineWordCount; ++i) {
    const size_t logical = lastBreakAt + (willReorder ? visualOrderScratch[i] : i);
    if (!paragraphStartEmitted && logical == 0) paragraphStartWord = static_cast<uint16_t>(i);
    const uint32_t start = visibleOffsetAt(logical);
    const bool discretionaryHyphen = (storedStyleAt(logical) & TextBlock::DISCRETIONARY_HYPHEN_FLAG) != 0;
    uint32_t end = sourceOffsetAfter(start, countCodepoints(wordAt(logical)) - discretionaryHyphen);
    if (discretionaryHyphen) {
      lineWordStyles[i] = static_cast<EpdFontFamily::Style>(lineWordStyles[i] | TextBlock::DISCRETIONARY_HYPHEN_FLAG);
    }
    if (logical + 1 < tokenCount() && visibleOffsetAt(logical + 1) > start) {
      end = std::min(end, visibleOffsetAt(logical + 1));
    }
    sourceRanges.push_back({start, end});
  }

#endif

  if (!lineHasFocusSplit) {
    // TextBlock flattens the vectors into its arena; they stay owned here and die at return.
    auto block = makeUniqueNoThrow<TextBlock>(
#if XP_RUST_LAYOUT
        TextBlock::BorrowedLayout{},
#endif
        lineWords, lineXPos, lineWordStyles, std::vector<uint8_t>{}, std::vector<uint16_t>{}, blockStyle,
        std::move(lineRubyTexts), std::move(lineLinks), sourceRanges, paragraphStartWord);
    if (!block || !block->valid() || !attachWordFonts(*block)) {
      LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
      // Latch through the same flag as addWord() OOM: the caller releases the
      // consumed words right after this returns, so without it the section
      // would commit with this line silently missing.
      droppedWords = true;
      return;
    }
    processLine(std::move(block), lineVisibleOffset);
    return;
  }

  // Each word is one TextBlock entry carrying its own boundary; all that remains is the suffix x
  // offset the renderer needs to resume in regular weight, i.e. the bold prefix's advance.
#if XP_RUST_LAYOUT
  auto outBoundaries = extracted.boundaries();
  auto outSuffixX = extracted.suffixes();
#else
  std::vector<uint8_t> outBoundaries;
  std::vector<uint16_t> outSuffixX;
  outBoundaries.reserve(lineWordCount);
  outSuffixX.reserve(lineWordCount);
#endif
  for (size_t i = 0; i < lineWordCount; i++) {
    const uint8_t boundary = focusBoundaryAt(i);
#if XP_RUST_LAYOUT
    outBoundaries[i] = boundary;
#else
    outBoundaries.push_back(boundary);
#endif
    const int wordFont = wordFontId(lastBreakAt + (willReorder ? visualOrderScratch[i] : i), fontId);
    const uint16_t suffixX =
        boundary == 0
            ? 0
            : measureFocusPrefixAdvance(renderer, wordFont, lineWords[i],
                                        getWordStyleAt(lastBreakAt + (willReorder ? visualOrderScratch[i] : i)),
                                        boundary, blockStyle.characterSpacing);
#if XP_RUST_LAYOUT
    outSuffixX[i] = suffixX;
#else
    outSuffixX.push_back(suffixX);
#endif
  }

  auto block = makeUniqueNoThrow<TextBlock>(
#if XP_RUST_LAYOUT
      TextBlock::BorrowedLayout{},
#endif
      lineWords, lineXPos, lineWordStyles, outBoundaries, outSuffixX, blockStyle, std::move(lineRubyTexts),
      std::move(lineLinks), sourceRanges, paragraphStartWord);
  if (!block || !block->valid() || !attachWordFonts(*block)) {
    LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
    droppedWords = true;  // see the non-focus branch above
    return;
  }
  processLine(std::move(block), lineVisibleOffset);
}
