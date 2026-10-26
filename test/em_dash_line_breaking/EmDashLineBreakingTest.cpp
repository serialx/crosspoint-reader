#include <Epub/Paragraph.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
};

// Fixture metrics (stub renderer): every glyph is 8 px wide and a space is 4 px.
std::vector<Line> layout(const std::vector<const char*>& words, const bool hyphenation, const uint16_t width) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  Paragraph text(hyphenation, false, style, 0);
  for (const char* word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
  });
  return lines;
}

std::vector<std::vector<std::string>> wordsOf(const std::vector<Line>& lines) {
  std::vector<std::vector<std::string>> result;
  result.reserve(lines.size());
  for (const auto& line : lines) result.push_back(line.words);
  return result;
}

}  // namespace

TEST(EmDashLineBreaking, HyphenationOffSplitsAcrossLines) {
  // "foo—" is 4 chars * 8 px = 32 px, fits in 40 px.
  // "bar" is 3 chars * 8 px = 24 px. Total "foo—bar" is 56 px, which exceeds 40 px.
  const auto lines = layout({"foo—bar"}, /*hyphenation=*/false, 40);
  const std::vector<std::vector<std::string>> expected{{"foo—"}, {"bar"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(EmDashLineBreaking, HyphenationOffKeepsTogetherOnSameLineWithoutGap) {
  // 80 px fits "foo—" (32 px) and "bar" (24 px).
  const auto lines = layout({"foo—bar"}, /*hyphenation=*/false, 80);
  const std::vector<std::vector<std::string>> expected{{"foo—", "bar"}};
  ASSERT_EQ(wordsOf(lines), expected);
  // "bar" attaches directly to "foo—" with kerning 0, so xpos is exactly 0 and 32.
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 32}));
}

TEST(EmDashLineBreaking, MultiEmDashStaysTogether) {
  // "foo——" is 5 chars * 8 px = 40 px. Width 45 fits "foo——", but not "bar".
  const auto lines = layout({"foo——bar"}, /*hyphenation=*/false, 45);
  const std::vector<std::vector<std::string>> expected{{"foo——"}, {"bar"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(EmDashLineBreaking, MultipleEmDashesInWordCanBreakAtEitherDash) {
  // "foo—" (32 px), "bar—" (32 px), "baz" (24 px). Width 40 px.
  const auto lines = layout({"foo—bar—baz"}, /*hyphenation=*/false, 40);
  const std::vector<std::vector<std::string>> expected{{"foo—"}, {"bar—"}, {"baz"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(EmDashLineBreaking, HyphenationOnSplitsWithoutInsertedHyphen) {
  const auto lines = layout({"foo—bar"}, /*hyphenation=*/true, 40);
  const std::vector<std::vector<std::string>> expected{{"foo—"}, {"bar"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(EmDashLineBreaking, MixedCjkAndLatinSplitsAtDash) {
  // "漢" (8 px), "字—" (16 px), "bar" (24 px). Width 30 px fits "漢字—" (24 px), but not "bar".
  const auto lines = layout({"漢字—bar"}, /*hyphenation=*/false, 30);
  const std::vector<std::vector<std::string>> expected{{"漢", "字—"}, {"bar"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(EmDashLineBreaking, MixedLatinAndCjkSplitsAtDash) {
  // "foo—" (32 px), "漢" (8 px), "字" (8 px). Width 35 px fits "foo—", but wraps CJK text.
  const auto lines = layout({"foo—漢字"}, /*hyphenation=*/false, 35);
  const std::vector<std::vector<std::string>> expected{{"foo—"}, {"漢", "字"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(EmDashLineBreaking, MixedCjkMultiEmDashStaysTogether) {
  // "漢" (8 px), "字——" (24 px), "bar" (24 px). Width 35 px fits "漢字——", but wraps "bar".
  const auto lines = layout({"漢字——bar"}, /*hyphenation=*/false, 35);
  const std::vector<std::vector<std::string>> expected{{"漢", "字——"}, {"bar"}};
  EXPECT_EQ(wordsOf(lines), expected);
}

TEST(PartialLineLayout, WiderRemainderDoesNotKeepSpeculativeHyphens) {
  Hyphenator::setPreferredLanguage("en");
  for (const bool hyphenation : {false, true}) {
    for (const bool includeLastLine : {false, true}) {
      for (const size_t limit : {0u, 1u, 3u}) {
        SCOPED_TRACE(::testing::Message() << hyphenation << ", " << includeLastLine << ", " << limit);
        GfxRenderer renderer;
        Paragraph text(hyphenation, false, BlockStyle{}, 0);
        for (const char* word : {"nd",  "as",    "Jesus", "times",  "it",  "had", "laid",  "hold",  "him", "and",  "he",
                                 "was", "bound", "with",  "chains", "but", "he",  "would", "break", "the", "bonds"}) {
          text.addWord(word, EpdFontFamily::REGULAR);
        }
        const char* lastWord = hyphenation ? "asunder." : "abcdefghijklmnopqrstuvwxyz";
        text.addWord(lastWord, EpdFontFamily::REGULAR);
        size_t narrowLines = 0;
        text.layoutAndExtractLines(
            renderer, 0, 203,
            [&](std::unique_ptr<TextBlock>, auto) {
              ++narrowLines;
              EXPECT_EQ(text.linesAfterCurrentLine(), -1);
            },
            includeLastLine, 0, 100, limit);
        EXPECT_EQ(narrowLines, limit);

        std::vector<std::string> remainingWords;
        text.layoutAndExtractLines(renderer, 0, 240, [&](std::unique_ptr<TextBlock> block, auto) {
          for (uint16_t i = 0; i < block->wordCount(); ++i) {
            if (block->wordHasDiscretionaryHyphen(i)) {
              EXPECT_EQ(i + 1, block->wordCount());
            }
            remainingWords.emplace_back(block->wordText(i));
          }
        });
        EXPECT_NE(std::find(remainingWords.begin(), remainingWords.end(), lastWord), remainingWords.end());
        EXPECT_TRUE(text.isEmpty());
      }
    }
  }
}

TEST(PartialLineLayout, HoldsTheParagraphLastLineUntilItIsComplete) {
  GfxRenderer renderer;
  Paragraph text(true, false, BlockStyle{}, 0);
  for (const char* word : {"one", "two", "three"}) text.addWord(word, EpdFontFamily::REGULAR);
  size_t lines = 0;
  const auto collect = [&](std::unique_ptr<TextBlock>, auto) { ++lines; };
  text.layoutAndExtractLines(renderer, 0, 60, collect, false, 0, 100, 1);
  EXPECT_EQ(lines, 1u);
  text.layoutAndExtractLines(renderer, 0, 200, collect, false, 0, 100, 3);
  EXPECT_EQ(lines, 1u);
  text.layoutAndExtractLines(renderer, 0, 200, collect);
  EXPECT_EQ(lines, 2u);
  EXPECT_TRUE(text.isEmpty());
}

TEST(PartialLineLayout, LastEmittedLineStillJustifiesWhenMoreTextRemains) {
  for (const bool hyphenation : {false, true}) {
    GfxRenderer renderer;
    Paragraph text(hyphenation, false, BlockStyle{}, 0);
    for (const char* word : {"one", "two", "three"}) text.addWord(word, EpdFontFamily::REGULAR);
    text.layoutAndExtractLines(
        renderer, 0, 80,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ASSERT_EQ(line->wordCount(), 2);
          EXPECT_EQ(line->wordXpos(1), 56);
        },
        true, 0, 100, 1);
  }
}

TEST(PartialLineLayout, BoundedLinesReserveRubyOverhangAtBothMargins) {
  for (const bool hyphenation : {false, true}) {
    for (const bool rubyFirst : {false, true}) {
      GfxRenderer renderer;
      Paragraph text(hyphenation, false, BlockStyle{}, 0);
      text.addWord(rubyFirst ? "a" : "word", EpdFontFamily::REGULAR);
      text.addWord(rubyFirst ? "word" : "a", EpdFontFamily::REGULAR);
      text.addWord("tail", EpdFontFamily::REGULAR);
      text.setRubyForWordAt(rubyFirst ? 0 : 1, "ruby");
      text.layoutAndExtractLines(
          renderer, 0, 52, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordCount(), 1); }, true, 0,
          100, 1);
    }
  }
}

TEST(PartialLineLayout, FollowingTextCanAbsorbRubyOverhang) {
  for (const bool hyphenation : {false, true}) {
    for (const bool attached : {false, true}) {
      for (const size_t limit : {size_t{1}, SIZE_MAX}) {
        GfxRenderer renderer;
        Paragraph text(hyphenation, false, BlockStyle{}, 0);
        text.addWord("a", EpdFontFamily::REGULAR);
        text.setRubyForWordAt(0, attached ? "ruby" : "longer");
        text.addWord("b", EpdFontFamily::REGULAR, false, attached);
        text.layoutAndExtractLines(
            renderer, 0, attached ? 36 : 56,
            [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordCount(), 2); }, true, 0, 100, limit);
        EXPECT_TRUE(text.isEmpty());
      }
    }
  }
}

TEST(ParagraphPackedText, SoftHyphenLimitUsesSanitizedBytes) {
  GfxRenderer renderer;
  Paragraph text(false, false, BlockStyle{}, 0);
  std::string chunk;
  for (size_t i = 0; i < 100; ++i) chunk += "\xc2\xad";
  for (size_t i = 0; i < 330; ++i) text.addWord(chunk, EpdFontFamily::REGULAR, false, i != 0, i * 100);
  text.addWord("a", EpdFontFamily::REGULAR, false, true, 33000);
  ASSERT_EQ(text.size(), 331u);
  size_t lines = 0;
  text.layoutAndExtractLines(renderer, 0, 480, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_TRUE(line->valid());
    ASSERT_EQ(line->wordCount(), 331u);
    for (uint16_t i = 0; i < 331; ++i) {
      EXPECT_STREQ(line->wordText(i), i == 330 ? "a" : "");
      EXPECT_EQ(line->wordSourceRange(i).start, i * 100u);
      EXPECT_EQ(line->wordSourceRange(i).end, i == 330 ? 33001u : (i + 1) * 100u);
    }
  });
  EXPECT_EQ(lines, 1u);
  EXPECT_FALSE(text.hadDroppedWords());
}

TEST(ParagraphPackedText, SanitizedSizeBoundary) {
  for (const size_t total : {65534u, 65535u, 65536u}) {
    SCOPED_TRACE(total);
    GfxRenderer renderer;
    Paragraph text(false, false, BlockStyle{}, 0);
    std::vector<std::string> expected;
    size_t remaining = total;
    while (remaining != 0) {
      const size_t bytes = std::min(remaining, size_t{200});
      std::string word;
      for (size_t i = 0; i < (bytes - 1) / 2; ++i) word += "\xcc\x80";
      if ((bytes - 1) % 2) word += 'a';
      expected.push_back(word);
      text.addWord("\xc2\xad" + word, EpdFontFamily::REGULAR, false, !text.isEmpty());
      remaining -= bytes;
    }
    size_t lines = 0;
    text.layoutAndExtractLines(renderer, 0, UINT16_MAX, [&](std::unique_ptr<TextBlock> line, auto) {
      ++lines;
      ASSERT_TRUE(line->valid());
      ASSERT_EQ(line->wordCount(), expected.size());
      for (uint16_t i = 0; i < line->wordCount(); ++i) EXPECT_EQ(line->wordText(i), expected[i]);
    });
    EXPECT_EQ(lines, total <= UINT16_MAX ? 1u : 0u);
    EXPECT_EQ(text.hadDroppedWords(), total > UINT16_MAX);
  }
}

TEST(TextBlockConstruction, VectorAndPackedInputsPreserveRtlAndFocusMetadata) {
  const std::vector<std::string> words{"العربية", "word"};
  const std::vector<int16_t> positions{45, 0};
  const std::vector<EpdFontFamily::Style> styles{EpdFontFamily::REGULAR, EpdFontFamily::BOLD};
  const std::vector<uint8_t> focus{0, 2};
  const std::vector<uint16_t> suffix{0, 16};
  const std::vector<TextBlock::SourceRange> ranges{{7, 14}, {0, 4}};
  std::string packed;
  std::vector<uint16_t> offsets;
  for (const auto& word : words) {
    offsets.push_back(packed.size());
    packed += word;
    packed += '\0';
  }
  BlockStyle style;
  style.isRtl = true;
  TextBlock dictionary(words, positions, styles, focus, suffix, style, {}, {}, ranges, 1);
  TextBlock paragraph(TextBlock::BorrowedLayout{}, std::span<const char>(packed.data(), packed.size()), offsets,
                      positions, styles, focus, suffix, style, {}, {}, ranges, 1);
  ASSERT_TRUE(dictionary.valid());
  ASSERT_TRUE(paragraph.valid());
  ASSERT_EQ(dictionary.wordCount(), paragraph.wordCount());
  for (uint16_t i = 0; i < dictionary.wordCount(); ++i) {
    EXPECT_STREQ(dictionary.wordText(i), paragraph.wordText(i));
    EXPECT_EQ(dictionary.wordXpos(i), paragraph.wordXpos(i));
    EXPECT_EQ(dictionary.wordStyle(i), paragraph.wordStyle(i));
    EXPECT_EQ(dictionary.focusBoundary(i), paragraph.focusBoundary(i));
    EXPECT_EQ(dictionary.focusSuffixX(i), paragraph.focusSuffixX(i));
    EXPECT_EQ(dictionary.wordSourceRange(i).start, paragraph.wordSourceRange(i).start);
    EXPECT_EQ(dictionary.wordSourceRange(i).end, paragraph.wordSourceRange(i).end);
    EXPECT_EQ(dictionary.wordStartsParagraph(i), paragraph.wordStartsParagraph(i));
  }
}
