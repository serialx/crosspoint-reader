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
#include <string_view>
#include <vector>

#include "CjkBreaks.h"
#include "TokenBoundary.h"
#include "hyphenation/Hyphenator.h"

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
// Paragraph-level direction: scan the first N words to find base direction.
constexpr size_t RTL_PARAGRAPH_PROBE_WORDS = 3;
// Per-word: scan enough chars to see through leading neutrals (quotes, numbers)
// before giving up. 64 is a hedge for pathological cases like long numeric tokens.
constexpr int RTL_PER_WORD_PROBE_DEPTH = 64;

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

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  word.resize(xp_layout_strip_soft_hyphens(reinterpret_cast<uint8_t*>(word.data()), word.size()));
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
  if (!wordWidths.valid()) {
    droppedWords = true;
    return;
  }

  ParagraphBreaks lineBreakIndices;
  lineBreakIndices = computeRustLineBreaks(renderer, fontId, pageWidth, wordWidths, maxLines);
  if (lineBreakIndices.empty() || !wordWidths.valid()) {
    LOG_ERR("PTX", "OOM: paragraph line breaks");
    droppedWords = true;
    return;
  }
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
  if (!wordWidths.valid()) return wordWidths;

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
        const auto flags = p.tokenAt(i).flags;
        return TokenBoundary::allowsBreak((flags & 1) != 0, (flags & 2) != 0);
      },
      [](void* raw, size_t i) -> int32_t {
        const auto& c = *static_cast<Context*>(raw);
        const auto& p = c.paragraph;
        const auto flags = p.tokenAt(i).flags;
        if ((flags & 1) != 0) {
          return c.renderer.getKerning(c.fontId, lastCodepoint(p.wordAt(i - 1)), firstCodepoint(p.wordAt(i)),
                                       p.getWordStyleAt(i - 1), p.blockStyle.characterSpacing);
        }
        if ((flags & 2) != 0) return p.blockStyle.characterSpacing;
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

// Splits storedWordAt(wordIndex) into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint
// fits the available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, ParagraphWidths& wordWidths, const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= tokenCount()) {
    return false;
  }

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
  bool hasFocus = false;
  for (size_t i = lastBreakAt; i < lineBreak; ++i) hasFocus |= tokenAt(i).focus != 0;
  ParagraphLine extracted(lineWordCount, hasFocus);
  if (!extracted.valid()) {
    LOG_ERR("PTX", "OOM: line metadata (%zu words)", lineWordCount);
    droppedWords = true;
    return;
  }

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
  auto lineWordStyles = extracted.styles();

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
    lineWordStyles[i] = static_cast<EpdFontFamily::Style>(getWordStyleAt(lastBreakAt + i) & ~EpdFontFamily::SMALL_CAPS);
  }

  const bool isLastLine = lineBreak == tokenCount();

  // For RTL, implicit/default Left alignment becomes Right alignment.
  // Explicit text-align:left must remain left for CSS correctness.
  const CssTextAlign effectiveAlignment =
      (blockStyle.isRtl && !blockStyle.textAlignDefined && blockStyle.alignment == CssTextAlign::Left)
          ? CssTextAlign::Right
          : blockStyle.alignment;

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
        const auto flags = c.reordered ? 0 : p.tokenAt(c.start + i).flags;
        const bool continues = c.reordered ? p.reorderedContinuesScratch[i] : (flags & 1) != 0;
        const bool noSpace = c.reordered ? p.reorderedNoSpaceBeforeScratch[i] : (flags & 2) != 0;
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

  // BiDi processing: reorder words with UAX#9 in full-line context.
  visualOrderScratch.clear();
  visualOrderScratch.reserve(lineWordCount);
  // Skip expensive visual-order resolution for pure LTR paragraphs that have no RTL words.
  const bool shouldResolveVisualOrder = blockStyle.isRtl || hasRtlWord;
  const bool willReorder =
      shouldResolveVisualOrder && BidiUtils::computeVisualWordOrder(lineWords, blockStyle.isRtl, visualOrderScratch);

  auto lineXPos = extracted.positions();

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

    positionContext.reordered = true;
    lineSpec.reordered = 1;
    XpLineMetrics visualMetrics{};
    xp_layout_line_metrics(&lineSpec, &lineCallbacks, &visualMetrics);
    // Rust visual placement.
    xp_layout_line_position(&lineSpec, &lineCallbacks, &visualMetrics, lineXPos.data());

    lineWords.swap(reorderedWordsScratch);
    std::copy(reorderedStylesScratch.begin(), reorderedStylesScratch.end(), lineWordStyles.begin());
  } else {
    xp_layout_line_position(&lineSpec, &lineCallbacks, &logicalMetrics, lineXPos.data());
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

  auto sourceRanges = extracted.sources();
  uint16_t paragraphStartWord = UINT16_MAX;
  if (!xp_layout_line_sources(&extracted.state, wordStore.handle(), tokens(), lastBreakAt,
                              willReorder ? visualOrderScratch.data() : nullptr, paragraphStartEmitted,
                              &paragraphStartWord)) {
    LOG_ERR("PTX", "Invalid line source range");
    droppedWords = true;
    return;
  }

  if (!lineHasFocusSplit) {
    // TextBlock flattens the vectors into its arena; they stay owned here and die at return.
    auto block =
        makeUniqueNoThrow<TextBlock>(TextBlock::BorrowedLayout{}, lineWords, lineXPos, lineWordStyles,
                                     std::vector<uint8_t>{}, std::vector<uint16_t>{}, blockStyle,
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
  auto outBoundaries = extracted.boundaries();
  auto outSuffixX = extracted.suffixes();
  for (size_t i = 0; i < lineWordCount; i++) {
    const uint8_t boundary = focusBoundaryAt(i);
    outBoundaries[i] = boundary;
    const int wordFont = wordFontId(lastBreakAt + (willReorder ? visualOrderScratch[i] : i), fontId);
    const uint16_t suffixX =
        boundary == 0
            ? 0
            : measureFocusPrefixAdvance(renderer, wordFont, lineWords[i],
                                        getWordStyleAt(lastBreakAt + (willReorder ? visualOrderScratch[i] : i)),
                                        boundary, blockStyle.characterSpacing);
    outSuffixX[i] = suffixX;
  }

  auto block = makeUniqueNoThrow<TextBlock>(TextBlock::BorrowedLayout{}, lineWords, lineXPos, lineWordStyles,
                                            outBoundaries, outSuffixX, blockStyle, std::move(lineRubyTexts),
                                            std::move(lineLinks), sourceRanges, paragraphStartWord);
  if (!block || !block->valid() || !attachWordFonts(*block)) {
    LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
    droppedWords = true;  // see the non-focus branch above
    return;
  }
  processLine(std::move(block), lineVisibleOffset);
}
