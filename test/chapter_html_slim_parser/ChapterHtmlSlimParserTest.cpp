#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/converters/ImageDimsProbe.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "src/ReaderFontSizes.h"
#include "src/activities/settings/TextSettingsPreview.h"
#include "src/clippings/ClippingText.h"
#include "src/util/ParagraphIndentMigration.h"

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class
#include "../../src/fontIds.h"

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();

extern "C" void xp_chapter_test_options(XpChapterState*, const XpCss*, const XpChapterOptions*);
extern "C" void xp_chapter_test_event(XpChapterState*, uint32_t, const XpChapterSpan*, uint32_t);
extern "C" uint32_t xp_chapter_test_query(const XpChapterState*, uint32_t);
extern "C" void xp_chapter_test_columns(XpChapterState*, uint16_t, uint16_t);

extern "C" XpParagraph* xp_chapter_test_paragraph(XpChapterState*);
extern "C" uint32_t xp_chapter_test_assembly(XpChapterState*, uint32_t, uint32_t);
extern "C" void xp_chapter_test_carry(XpChapterState*, uint32_t, uint32_t, uint32_t);
extern "C" void xp_chapter_test_pending_note(const XpChapterState*, uint32_t, XpChapterFootnote*);
extern "C" void xp_chapter_test_note(XpChapterState*, const XpChapterFootnote*);
extern "C" void xp_chapter_test_link(XpChapterState*, const XpChapterLink*);

struct ParagraphView {
  XpParagraph* value;
  XpParagraph* handle() const { return value; }
  void addWord(std::string_view word, EpdFontFamily::Style style) {
    xp_para_add_word(value, {reinterpret_cast<const uint8_t*>(word.data()), static_cast<uint32_t>(word.size())}, style,
                     0, 0, 0, 0, 0, 0);
  }
  size_t size() const { return xp_para_size(value); }
  size_t tokenCount() const { return size(); }
  XpToken tokenAt(size_t index) const {
    XpToken result{};
    xp_para_token(value, index, &result);
    return result;
  }
  EpdFontFamily::Style getWordStyleAt(size_t index) const {
    return static_cast<EpdFontFamily::Style>(xp_para_word_style(value, index));
  }
  std::string_view wordAt(size_t index) const {
    const auto word = xp_para_word(value, index);
    return {reinterpret_cast<const char*>(word.data), word.len};
  }
  bool linkTargetMatches(uint8_t id, const char* href) const {
    return xp_para_link_matches(value, id,
                                {reinterpret_cast<const uint8_t*>(href), static_cast<uint32_t>(strlen(href))});
  }
  void setRubyGroupAt(size_t index, size_t count, const std::string& text) {
    xp_para_set_ruby_group(value, index, count,
                           {reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size())});
  }
  BlockStyle getBlockStyle() const {
    XpParaBlock wire{};
    xp_para_get_style(value, &wire);
    BlockStyle result;
    result.characterSpacing = wire.character_spacing;
    result.marginTop = wire.margin_top;
    result.marginBottom = wire.margin_bottom;
    result.marginLeft = wire.margin_left;
    result.marginRight = wire.margin_right;
    result.paddingTop = wire.padding_top;
    result.paddingBottom = wire.padding_bottom;
    result.paddingLeft = wire.padding_left;
    result.paddingRight = wire.padding_right;
    result.textIndent = wire.text_indent;
    result.textIndentDefined = wire.text_indent_defined;
    result.textAlignDefined = wire.text_align_defined;
    result.isRtl = wire.rtl;
    result.directionDefined = wire.direction_defined;
    result.fontScale = wire.font_scale;
    result.fontScaleDefined = wire.font_scale_defined;
    result.fontId = wire.font_id;
    result.pageBreakBefore = wire.break_before;
    result.pageBreakAfter = wire.break_after;
    result.keepWithNext = wire.keep_with_next;
    result.alignment = static_cast<CssTextAlign>(wire.alignment);
    return result;
  }
};

// Direct callbacks remain host-only: firmware feeds XML wholly inside Rust.
struct ChapterTestAccess {
  static XpChapterState* state(ChapterHtmlSlimParser& parser) {
    const bool fresh = parser.chapter_ == nullptr;
    if (!parser.ensureSemantics()) throw std::bad_alloc();
    const auto options = parser.chapterOptions();
    xp_chapter_test_options(parser.semanticState(), parser.cssParser ? parser.cssParser->rustHandle() : nullptr,
                            &options);
    if (fresh) xp_chapter_test_assembly(parser.semanticState(), 100, 0);
    return parser.semanticState();
  }
  static uint32_t assembly(ChapterHtmlSlimParser& parser, uint32_t operation, uint32_t value = 0) {
    return xp_chapter_test_assembly(state(parser), operation, value);
  }
  static ParagraphView paragraph(ChapterHtmlSlimParser& parser) { return {xp_chapter_test_paragraph(state(parser))}; }
  static void begin(ChapterHtmlSlimParser& parser) {
    state(parser);
    parser.beginAssembly();
  }
  static bool finish(ChapterHtmlSlimParser& parser) {
    state(parser);
    return parser.finishParse();
  }
  static int font(ChapterHtmlSlimParser& parser, float scale) {
    uint32_t bits;
    memcpy(&bits, &scale, 4);
    return static_cast<int32_t>(assembly(parser, 7, bits));
  }
  static std::unique_ptr<Page> takePage(ChapterHtmlSlimParser& parser) {
    std::unique_ptr<Page> result;
    auto saved = std::move(parser.completePageFn);
    parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) { result = std::move(page); };
    assembly(parser, 104);
    parser.completePageFn = std::move(saved);
    return result;
  }
  static FootnoteEntry pendingNote(ChapterHtmlSlimParser& parser, size_t index) {
    XpChapterFootnote wire{};
    xp_chapter_test_pending_note(state(parser), index, &wire);
    FootnoteEntry result;
    memcpy(result.number, wire.number, sizeof(result.number));
    memcpy(result.href, wire.href, sizeof(result.href));
    return result;
  }
  static void addNote(ChapterHtmlSlimParser& parser, const char* number, const char* href) {
    XpChapterFootnote note{};
    strncpy(reinterpret_cast<char*>(note.number), number, 31);
    strncpy(reinterpret_cast<char*>(note.href), href, 255);
    xp_chapter_test_note(state(parser), &note);
  }
  static void addLink(ChapterHtmlSlimParser& parser, const char* href, int16_t x, int16_t y, int16_t width,
                      int16_t height) {
    XpChapterLink link{};
    strncpy(reinterpret_cast<char*>(link.href), href, 255);
    link.x = x;
    link.y = y;
    link.width = width;
    link.height = height;
    xp_chapter_test_link(state(parser), &link);
  }
  static XpChapterSpan span(const char* value, size_t length) {
    return {reinterpret_cast<const uint8_t*>(value), static_cast<uint32_t>(length)};
  }
  static void startElement(ChapterHtmlSlimParser* parser, const char* name, const char* const* attrs) {
    std::vector<XpChapterSpan> fields{span(name, strlen(name))};
    if (attrs)
      for (size_t i = 0; attrs[i]; ++i) fields.push_back(span(attrs[i], strlen(attrs[i])));
    xp_chapter_test_event(state(*parser), 0, fields.data(), fields.size());
  }
  static void characterData(ChapterHtmlSlimParser* parser, const char* text, int length) {
    const auto field = span(text, length);
    xp_chapter_test_event(state(*parser), 1, &field, 1);
  }
  static void endElement(ChapterHtmlSlimParser* parser, const char* name) {
    const auto field = span(name, strlen(name));
    xp_chapter_test_event(state(*parser), 2, &field, 1);
  }
  static uint32_t query(ChapterHtmlSlimParser& parser, uint32_t field) {
    return xp_chapter_test_query(state(parser), field);
  }
  static void columns(ChapterHtmlSlimParser& parser, uint16_t first, uint16_t second) {
    xp_chapter_test_columns(state(parser), first, second);
  }
};

namespace {

void installBuiltinVariants(GfxRenderer& renderer) {
  renderer.setFontVariantProvider({&renderer, [](void* ctx, const int fontId, const float scale) {
                                     const int variant = builtinReaderFontVariant(fontId, scale);
                                     return static_cast<GfxRenderer*>(ctx)->getFontMap().count(variant) ? variant : 0;
                                   }});
}

void installSyntheticVariants(GfxRenderer& renderer) {
  renderer.setFontVariantProvider({&renderer,
                                   [](void* ctx, const int fontId, const float scale) {
                                     const int variant = fontId * 1000 + static_cast<int>(scale * 100 + 0.5f);
                                     static_cast<GfxRenderer*>(ctx)->fontMap.emplace(variant, EpdFontFamily(nullptr));
                                     return variant;
                                   },
                                   [](void* ctx, const int fontId) {
                                     static_cast<GfxRenderer*>(ctx)->fontMap.emplace(fontId, EpdFontFamily(nullptr));
                                     return true;
                                   }});
}

static std::vector<FootnoteEntry> notes(const Page& page) {
  std::vector<FootnoteEntry> result;
  page.copyFootnotes(result);
  return result;
}
static std::vector<PageLink> pageLinks(const Page& page) {
  std::vector<PageLink> result;
  page.copyLinks(result);
  return result;
}

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  void SetUp() override {}
  void TearDown() override { Epub::enableGifProbeForTest = false; }

  // Lays out a body fragment through the cached-page path and joins its words the way a
  // clipping does: '\n' at a paragraph start, ' ' across a source gap, nothing otherwise.
  std::string parseToParagraphText(const std::string& body, const std::string& prefix = "",
                                   const std::string& suffix = "") {
    const std::string html = prefix + "<html><body>" + body + "</body></html>" + suffix;
    const std::string testId = std::to_string(std::hash<std::string>{}(html));
    const auto tempDir = std::filesystem::temp_directory_path();
    filepath = (tempDir / ("crosspoint-clipping-paragraphs-" + testId + ".xhtml")).string();
    {
      HalFile file;
      EXPECT_TRUE(file.open(filepath.c_str(), "wb"));
      EXPECT_EQ(file.write(html.data(), html.size()), html.size());
    }
    const auto cachePath = (tempDir / ("crosspoint-clipping-paragraphs-" + testId + ".bin")).string();
    std::string selected;
    uint32_t previousEnd = 0;
    bool paragraphStartPending = false;
    parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
      {
        HalFile file;
        ASSERT_TRUE(file.open(cachePath.c_str(), "wb"));
        ASSERT_TRUE(page->serialize(file));
      }
      HalFile file;
      ASSERT_TRUE(file.open(cachePath.c_str(), "rb"));
      auto cached = Page::load(file, file.size() - file.position());
      ASSERT_NE(cached, nullptr);
      for (uint16_t index = 0; index < cached->elementCount(); ++index) {
        const auto element = cached->element(index);
        if (element.getTag() != TAG_PageLine) continue;
        const auto block = element.textBlock();
        for (uint16_t i = 0; i < block.wordCount(); ++i) {
          if (block.wordStartsParagraph(i)) paragraphStartPending = true;
          if (!clippingText::hasVisibleText(block.wordText(i))) continue;
          const auto range = block.wordSourceRange(i);
          const char separator = paragraphStartPending ? '\n' : range.start > previousEnd ? ' ' : '\0';
          ASSERT_TRUE(clippingText::append(selected, block.wordText(i), separator, 4096));
          paragraphStartPending = false;
          previousEnd = range.end;
        }
      }
      EXPECT_EQ(file.position(), file.size());
    };
    parser.paragraphIndentSpaces = 0;
    EXPECT_TRUE(parser.parseAndBuildPages());
    std::filesystem::remove(filepath);
    std::filesystem::remove(cachePath);
    return selected;
  }
};

TEST_F(ChapterHtmlSlimParserTest, MapsSkippedEntitiesOnlyInText) {
  EXPECT_EQ(parseToParagraphText("<p id='a&copy;b'>&copy; &emsp; &notMapped; &amp; &#169;</p>",
                                 "<!DOCTYPE html SYSTEM 'unused'>"),
            "© &notMapped; & ©");
  ASSERT_EQ(parser.getAnchors().size(), 1u);
  EXPECT_EQ(parser.getAnchors().front().first, "ab");
}

TEST_F(ChapterHtmlSlimParserTest, DeclaredEntitiesKeepGeZeroLiteralText) {
  EXPECT_EQ(parseToParagraphText("<p>&copy; &word;</p>",
                                 "<!DOCTYPE html [<!ENTITY copy 'replacement'><!ENTITY word 'other'>]>"),
            "&copy; &word;");
}

TEST_F(ChapterHtmlSlimParserTest, PreservesLongUnknownEntitySpelling) {
  parser.viewportWidth = 32767;  // Keep the entity on one line without layout hyphens.
  const std::string entity = "&" + std::string(2000, 'x') + ";";
  EXPECT_EQ(parseToParagraphText("<p>" + entity + "</p>", "<!DOCTYPE html SYSTEM 'unused'>"), entity);
}

TEST_F(ChapterHtmlSlimParserTest, IgnoresMalformedTrailingBytesAfterHtml) {
  EXPECT_EQ(parseToParagraphText("<p>kept text</p>", "", "trailing<broken"), "kept text");
}

TEST_F(ChapterHtmlSlimParserTest, MapsTableOffsetsAfterMultibytePrefix) {
  EXPECT_EQ(parseToParagraphText("<p>한글</p><table><tr><td>one two</td><td>three four five</td></tr>"
                                 "<tr><td>six seven eight</td><td>nine ten</td></tr></table>"),
            "한글\none two\nthree four five\nsix seven eight\nnine ten");
}

TEST_F(ChapterHtmlSlimParserTest, XmlHandleOomDoesNotOpenInput) {
  xp_test_fail_allocation_after(0);
  const bool started = parser.beginParse();
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_FALSE(started);
  EXPECT_EQ(parser.chapter_, nullptr);
  EXPECT_FALSE(parser.parseFile_.isOpen());
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}

TEST_F(ChapterHtmlSlimParserTest, XmlFeedOomFailsAndReleasesInput) {
  filepath = (std::filesystem::temp_directory_path() / "crosspoint-parser-feed-oom.xhtml").string();
  const std::string html = "<html><body><p>must not succeed with missing text</p></body></html>";
  {
    HalFile file;
    ASSERT_TRUE(file.open(filepath.c_str(), "wb"));
    ASSERT_EQ(file.write(html.data(), html.size()), html.size());
  }
  ASSERT_TRUE(parser.beginParse());
  xp_test_fail_allocation_after(0);
  const auto status = parser.parseStep();
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_EQ(status, ChapterHtmlSlimParser::ParseStatus::Error);
  parser.abortParse();
  EXPECT_EQ(parser.chapter_, nullptr);
  EXPECT_EQ(parser.parseBuffer_, nullptr);
  EXPECT_FALSE(parser.parseFile_.isOpen());
  EXPECT_EQ(xp_test_live_allocations(), 0u);
  std::filesystem::remove(filepath);
}

TEST_F(ChapterHtmlSlimParserTest, ChapterAllocationFailuresReleaseEveryRustOwner) {
  std::filesystem::path fixtureDirectory;
  for (size_t suffix = 0;; ++suffix) {
    fixtureDirectory =
        std::filesystem::temp_directory_path() / ("crosspoint-chapter-allocation-sweep-" + std::to_string(suffix));
    if (std::filesystem::create_directory(fixtureDirectory)) break;
  }
  filepath = (fixtureDirectory / "chapter.xhtml").string();
  const std::string html =
      "<!DOCTYPE html SYSTEM 'unused'><html><body><ol><li><span style='font-size:1.2em'>word</span> "
      "<ruby>base<rt>annotation</rt></ruby></li></ol><p>&" +
      std::string(1500, 'x') +
      ";</p><div style='border:1px solid;background:#eee'><h2>heading</h2>"
      "<p>one two three four five six <a href='#note' style='vertical-align:super'>1</a> "
      "seven eight nine ten eleven twelve</p><hr/><table><tr><td>left first second third</td>"
      "<td><ruby>right<rt>annotation</rt></ruby> fourth fifth sixth</td></tr></table>"
      "<img src='probe.gif'/></div></body></html>";
  {
    HalFile file;
    ASSERT_TRUE(file.open(filepath.c_str(), "wb"));
    ASSERT_EQ(file.write(html.data(), html.size()), html.size());
  }
  auto book = std::make_shared<Epub>();
  book->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 8, 0, 8, 0};
  Epub::enableGifProbeForTest = true;
  bool completed = false;
  size_t failures = 0;
  size_t required = 0;
  for (size_t budget = 0; budget == 0 || budget <= required + 1; ++budget) {
    SCOPED_TRACE(budget);
    {
      ChapterHtmlSlimParser candidate{book,  filepath, renderer, 0,  1.0f, false, 0,  160,     64,        false,
                                      false, {},       true,     "", "",   0,     {}, nullptr, &cssParser};
      candidate.completePageFn = [](std::unique_ptr<Page>, auto, auto, auto) {};
      xp_test_fail_allocation_after(budget == 0 ? SIZE_MAX : budget - 1);
      const char* stage = "begin";
      size_t consumed = 0;
      const auto parse = [&]() {
        if (!candidate.beginParse()) return false;
        for (;;) {
          stage = "feed";
          const auto status = candidate.parseStep();
          consumed = candidate.parseBytesConsumed();
          switch (status) {
            case ChapterHtmlSlimParser::ParseStatus::More:
              continue;
            case ChapterHtmlSlimParser::ParseStatus::Error:
              candidate.abortParse();
              return false;
            case ChapterHtmlSlimParser::ParseStatus::Done:
              stage = "finish";
              return candidate.finishParse();
          }
        }
      };
      completed = parse();
      std::fprintf(stderr,
                   "allocation sweep: fixture=%s budget=%zu attempts=%zu required=%zu stage=%s consumed=%zu "
                   "complete=%d exists=%d\n",
                   filepath.c_str(), budget == 0 ? SIZE_MAX : budget - 1, xp_test_allocation_attempts(), required,
                   stage, consumed, completed, std::filesystem::exists(filepath));
      if (budget == 0) required = xp_test_allocation_attempts();
      xp_test_fail_allocation_after(SIZE_MAX);
      if (!completed) ++failures;
      candidate.abortParse();
      EXPECT_EQ(candidate.chapter_, nullptr);
      EXPECT_FALSE(candidate.parseFile_.isOpen());
    }
    ASSERT_EQ(xp_test_live_allocations(), 0u);
    if (budget == 0) {
      ASSERT_TRUE(completed);
      continue;
    }
    // Sweep every allocation position, including tolerated optional allocation failures.
  }
  EXPECT_TRUE(completed);
  EXPECT_GT(failures, 10u);
  std::filesystem::remove_all(fixtureDirectory);
}

TEST(ImageDimsProbe, ReadsBothGifSignaturesInFragmentedStreams) {
  for (const uint8_t version : {'7', '9'}) {
    const uint8_t header[] = {'G', 'I', 'F', '8', version, 'a', 0, 12, 2, 0};
    for (size_t split = 0; split <= sizeof(header); ++split) {
      ImageDimsProbe probe;
      probe.write(header, split);
      ImageDimensions dims{};
      EXPECT_EQ(probe.getDimensions(dims), split == sizeof(header));
      probe.write(header + split, sizeof(header) - split);
      ASSERT_TRUE(probe.getDimensions(dims));
      EXPECT_EQ(dims.width, 3072);
      EXPECT_EQ(dims.height, 2);
      EXPECT_EQ(probe.write(0), 0u);
    }
  }
}

TEST(ImageDimsProbe, RejectsInvalidGifHeadersAndDimensions) {
  const std::vector<std::vector<uint8_t>> headers = {
      {'G', 'I', 'F', '8', '8', 'a', 1, 0, 1, 0},   {'G', 'I', 'F', '8', '9', 'b', 1, 0, 1, 0},
      {'G', 'I', 'F', '8', '9', 'a', 0, 0, 1, 0},   {'G', 'I', 'F', '8', '9', 'a', 1, 0, 0, 0},
      {'G', 'I', 'F', '8', '9', 'a', 0, 128, 1, 0},
  };
  for (const auto& header : headers) {
    ImageDimsProbe probe;
    probe.write(header.data(), header.size());
    ImageDimensions dims{};
    EXPECT_FALSE(probe.getDimensions(dims));
  }
}

TEST(ImageDimsProbe, PreservesJpegAndPngDimensions) {
  const std::vector<std::vector<uint8_t>> headers = {
      {0xFF, 0xD8, 0xFF, 0xC0, 0, 17, 8, 0, 2, 0, 3},
      {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 3, 0, 0, 0, 2},
  };
  for (const auto& header : headers) {
    for (size_t split = 0; split <= header.size(); ++split) {
      ImageDimsProbe probe;
      probe.write(header.data(), split);
      probe.write(header.data() + split, header.size() - split);
      ImageDimensions dims{};
      ASSERT_TRUE(probe.getDimensions(dims));
      EXPECT_EQ(dims.width, 3);
      EXPECT_EQ(dims.height, 2);
    }
  }
}

TEST_F(ChapterHtmlSlimParserTest, StreamsGifDimensionsWithoutExtractingPayload) {
  parser.epub = std::make_shared<Epub>();
  parser.epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0};
  parser.imageRendering = 0;
  const auto imagePath = std::filesystem::temp_directory_path() / "crosspoint-gif-probe-0.gif";
  std::filesystem::remove(imagePath);
  parser.imageBasePath = imagePath.string().substr(0, imagePath.string().size() - 5);
  Epub::enableGifProbeForTest = true;
  const char* attrs[] = {"src", "probe.gif", nullptr};
  ChapterTestAccess::startElement(&parser, "body", nullptr);
  ChapterTestAccess::startElement(&parser, "img", attrs);
  EXPECT_EQ(parser.epub->headerProbes, 1u);
  EXPECT_EQ(parser.epub->fullExtractions, 0u);
  EXPECT_FALSE(std::filesystem::exists(imagePath));
  std::filesystem::remove(imagePath);
}

TEST_F(ChapterHtmlSlimParserTest, AbandonedLayoutStopsExtractingLines) {
  Paragraph text;
  for (int i = 0; i < 100; ++i) text.addWord("word", EpdFontFamily::REGULAR);
  bool stop = false;
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 80,
      [&](std::unique_ptr<TextBlock>, uint32_t) {
        ++lines;
        stop = true;
      },
      true, 0, 100, SIZE_MAX, &stop);
  EXPECT_EQ(lines, 1u);
}

class ParserStopTest : public ChapterHtmlSlimParserTest {};

TEST_P(ParserStopTest, PageCallbackStopsParsingAndClosesFile) {
  filepath = (std::filesystem::temp_directory_path() /
              ("crosspoint-parser-stop-" + std::to_string(std::hash<std::string>{}(GetParam())) + ".xhtml"))
                 .string();
  std::string html = "<html><body>";
  for (int i = 0; i < 100; ++i) html += GetParam();
  html += "</body></html>";
  {
    HalFile file;
    ASSERT_TRUE(file.open(filepath.c_str(), "wb"));
    ASSERT_EQ(file.write(html.data(), html.size()), html.size());
  }
  renderer.sdFont = true;
  parser.viewportWidth = 160;
  parser.viewportHeight = 32;
  unsigned pages = 0;
  size_t fontPreparationsAtStop = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ASSERT_NE(page, nullptr);
    ++pages;
    fontPreparationsAtStop = renderer.fontPreparations;
    parser.requestStop();
  };
  EXPECT_FALSE(parser.parseAndBuildPages());
  EXPECT_EQ(pages, 1u);
  EXPECT_EQ(renderer.fontPreparations, fontPreparationsAtStop);
  EXPECT_EQ(parser.chapter_, nullptr);
  EXPECT_FALSE(parser.parseFile_.isOpen());
  std::filesystem::remove(filepath);
}

INSTANTIATE_TEST_SUITE_P(
    FlowBoundaries, ParserStopTest,
    ::testing::Values("<p>one two three four five six seven eight</p>", "one two three four five six seven eight ",
                      "<p>one two three four five six</p><hr/>",
                      "<table><tr><td>one two three four</td><td>five six seven eight</td></tr></table>"));

TEST_F(ChapterHtmlSlimParserTest, StopDuringFinalFlushDoesNotEmitAnotherPage) {
  parser.viewportWidth = 80;
  parser.viewportHeight = 32;
  for (int i = 0; i < 100; ++i) ChapterTestAccess::paragraph(parser).addWord("word", EpdFontFamily::REGULAR);
  unsigned pages = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ASSERT_NE(page, nullptr);
    ++pages;
    parser.requestStop();
  };
  EXPECT_FALSE(ChapterTestAccess::finish(parser));
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  Paragraph text;
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_TRUE(line->getRubyTexts().empty());
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->getRubyTexts().size(), retainedWords);
    EXPECT_EQ(line->getRubyTexts().back(), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyTexts()[i].empty());
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  parser.viewportWidth = 240;
  parser.viewportHeight = 32;

  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    ChapterTestAccess::assembly(parser, 100);
    auto cell = ChapterTestAccess::paragraph(parser);
    for (int index = 0; index < (column == 0 ? 30 : 3); ++index) {
      const auto word = std::string(column == 0 ? "left" : "right") + std::to_string(index);
      expected.insert(word);
      cell.addWord(word, EpdFontFamily::REGULAR);
    }
    if (column == 0) cell.setRubyGroupAt(0, 2, "reading");
    ChapterTestAccess::assembly(parser, 106);
  }
  std::multiset<std::string> actual;
  unsigned pages = 0;
  unsigned rubyLines = 0;
  auto inspect = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    for (uint16_t elementIndex = 0; elementIndex < page->elementCount(); ++elementIndex) {
      const auto element = page->element(elementIndex);
      if (element.getTag() != TAG_PageLine) continue;
      const auto block = element.textBlock();
      ASSERT_TRUE(block.valid());
      EXPECT_LE(element.yPos + 16 + block.getRubyShift(12), parser.viewportHeight);
      rubyLines += block.hasRuby();
      for (uint16_t word = 0; word < block.wordCount(); ++word) actual.insert(block.wordText(word));
    }
  };
  parser.completePageFn = inspect;
  ChapterTestAccess::assembly(parser, 103);
  auto currentPage = ChapterTestAccess::takePage(parser);
  ASSERT_NE(currentPage, nullptr);
  inspect(std::move(currentPage), 0, 0, 0);
  EXPECT_GT(pages, 2u);
  EXPECT_EQ(rubyLines, 1u);
  EXPECT_EQ(actual, expected);
}

TEST(ReaderFontSizes, BuiltinVariantsStayInFamilyAndRespectSizeBounds) {
  for (const auto& family : {
           std::array{NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID},
       }) {
    EXPECT_EQ(builtinReaderFontVariant(family[1], 1.0f), family[1]);
    EXPECT_EQ(builtinReaderFontVariant(family[1], 2.0f), family[3]);
    EXPECT_EQ(builtinReaderFontVariant(family[1], 1.17f), family[2]);
    EXPECT_EQ(builtinReaderFontVariant(family[1], 0.83f), family[0]);
    EXPECT_EQ(builtinReaderFontVariant(family[3], 0.9f), family[2]);
    EXPECT_EQ(builtinReaderFontVariant(family[0], 0.5f), family[0]);
    EXPECT_EQ(builtinReaderFontVariant(family[3], 3.0f), family[3]);
    EXPECT_EQ(builtinReaderFontVariant(family[0], 1.25f), family[1]);  // ties keep the smaller size
  }
  EXPECT_EQ(builtinReaderFontVariant(12345, 2.0f), 0);
}

TEST_F(ChapterHtmlSlimParserTest, BlockFontScaleUsesProviderAndFallsBackToBodyFont) {
  installBuiltinVariants(renderer);
  for (const int id : {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID}) {
    renderer.fontMap.emplace(id, EpdFontFamily(nullptr));
  }
  parser.fontId = NOTOSANS_14_FONT_ID;
  EXPECT_EQ(ChapterTestAccess::font(parser, 1.0f), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(ChapterTestAccess::font(parser, 2.0f), NOTOSANS_18_FONT_ID);
  EXPECT_EQ(ChapterTestAccess::font(parser, 1.17f), NOTOSANS_16_FONT_ID);
  EXPECT_EQ(ChapterTestAccess::font(parser, 0.83f), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(ChapterTestAccess::font(parser, 0.96f), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(ChapterTestAccess::font(parser, 1.04f), NOTOSANS_14_FONT_ID);
  parser.fontId = 54321;  // family not registered with the renderer
  EXPECT_EQ(ChapterTestAccess::font(parser, 2.0f), 54321);
  parser.fontId = 12345;  // SD or vector font: never rescaled
  EXPECT_EQ(ChapterTestAccess::font(parser, 2.0f), 12345);
}

TEST(FontVariantProvider, NearUnitScalesKeepBaseFontWithoutResolving) {
  int calls = 0;
  FontVariantProvider provider{&calls, [](void* ctx, int, float) {
                                 ++*static_cast<int*>(ctx);
                                 return 42;
                               }};
  for (const int font : {NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, 7}) {
    for (const float scale : {0.96f, 0.99f, 1.0f, 1.04f}) EXPECT_EQ(provider.resolveFont(font, scale), font);
  }
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(provider.resolveFont(7, 0.94f), 42);
  EXPECT_EQ(calls, 1);
}

TEST_F(ChapterHtmlSlimParserTest, BuiltinFontSelectionDelegatesToInstalledProvider) {
  parser.fontId = NOTOSANS_14_FONT_ID;
  int calls = 0;
  renderer.setFontVariantProvider({&calls, [](void* ctx, const int fontId, const float scale) {
                                     ++*static_cast<int*>(ctx);
                                     EXPECT_EQ(fontId, NOTOSANS_14_FONT_ID);
                                     EXPECT_FLOAT_EQ(scale, 0.8f);
                                     return 42;
                                   }});
  EXPECT_EQ(ChapterTestAccess::font(parser, 0.8f), 42);
  EXPECT_EQ(calls, 1);
}

TEST_F(ChapterHtmlSlimParserTest, MissingVariantProviderFallsBackToBodyFont) {
  parser.fontId = NOTOSANS_14_FONT_ID;
  EXPECT_EQ(renderer.resolveFontVariant(parser.fontId, 2.0f), 0);
  EXPECT_EQ(ChapterTestAccess::font(parser, 2.0f), parser.fontId);
  EXPECT_EQ(ChapterTestAccess::font(parser, 0.96f), parser.fontId);
}

TEST_F(ChapterHtmlSlimParserTest, BlockFontScaleInheritsAndUsesHeadingDefaults) {
  ChapterTestAccess::startElement(&parser, "h2", nullptr);
  const auto heading = ChapterTestAccess::paragraph(parser).getBlockStyle();
  EXPECT_FLOAT_EQ(heading.fontScale, 1.5f);

  const char* em[] = {"style", "font-size: 0.5em", nullptr};
  ChapterTestAccess::startElement(&parser, "p", em);
  EXPECT_FLOAT_EQ(ChapterTestAccess::paragraph(parser).getBlockStyle().fontScale, 0.75f);
  ChapterTestAccess::endElement(&parser, "p");

  const char* rem[] = {"style", "font-size: 1.2rem", nullptr};
  ChapterTestAccess::startElement(&parser, "p", rem);
  EXPECT_FLOAT_EQ(ChapterTestAccess::paragraph(parser).getBlockStyle().fontScale, 1.2f);

  BlockStyle plain;
  const BlockStyle inherited = heading.getCombinedBlockStyle(plain, BlockStyle::CombineAxis::Horizontal);
  EXPECT_FLOAT_EQ(inherited.fontScale, 1.5f);
}

TEST_F(ChapterHtmlSlimParserTest, PageImageDeserializeRejectsMissingImageBlock) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-missing-image-cache.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const uint8_t prefix[] = {1, 0, TAG_PageImage};
    output.write(prefix, sizeof(prefix));
    const int16_t coordinates[] = {0, 0};
    output.write(coordinates, sizeof(coordinates));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  EXPECT_EQ(Page::load(input, input.size()), nullptr);
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterTestAccess::startElement(&parser, "a", attributes);
  const uint8_t linkId = ChapterTestAccess::query(parser, 0);
  ASSERT_NE(linkId, 0u);
  ChapterTestAccess::characterData(&parser, "1", 1);
  ChapterTestAccess::endElement(&parser, "a");

  ASSERT_EQ(ChapterTestAccess::paragraph(parser).size(), 1u);
  const auto style = ChapterTestAccess::paragraph(parser).getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(ChapterTestAccess::assembly(parser, 4), 1u);
  const FootnoteEntry& footnote = ChapterTestAccess::pendingNote(parser, 0);
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_EQ(ChapterTestAccess::paragraph(parser).tokenCount(), 1u);
  EXPECT_EQ(ChapterTestAccess::paragraph(parser).tokenAt(0).link, linkId);
  EXPECT_TRUE(ChapterTestAccess::paragraph(parser).linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

TEST_F(ChapterHtmlSlimParserTest, ParagraphWithHiddenAttributeShouldBeSkipped) {
  const char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "p", attributes);
  ChapterTestAccess::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(ChapterTestAccess::query(parser, 1), 0);
}

TEST_F(ChapterHtmlSlimParserTest, HeaderWithHiddenAttributeShouldBeSkipped) {
  const char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "h1", attributes);
  ChapterTestAccess::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(ChapterTestAccess::query(parser, 1), 0);
}

TEST_F(ChapterHtmlSlimParserTest, SpanWithHiddenAttributeShouldBeSkipped) {
  const char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "p", nullptr);
  ChapterTestAccess::characterData(&parser, "Before ", 7);
  ChapterTestAccess::startElement(&parser, "span", attributes);
  ChapterTestAccess::characterData(&parser, "[HIDDEN]", 8);
  ChapterTestAccess::endElement(&parser, "span");
  ChapterTestAccess::characterData(&parser, " After ", 7);

  ASSERT_EQ(ChapterTestAccess::paragraph(parser).size(), 2);
  ASSERT_EQ(ChapterTestAccess::paragraph(parser).wordAt(0), "Before");
  ASSERT_EQ(ChapterTestAccess::paragraph(parser).wordAt(1), "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const char* attributes[] = {"hidden", "hidden", nullptr};

  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "div", attributes);
  ChapterTestAccess::startElement(&parser, "p", nullptr);
  ChapterTestAccess::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(ChapterTestAccess::query(parser, 1), 0);
}

TEST_F(ChapterHtmlSlimParserTest, SmallCapsSpanMarksWordsWithoutChangingText) {
  const char* attributes[] = {"style", "font-variant: small-caps", nullptr};
  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "p", nullptr);
  ChapterTestAccess::characterData(&parser, "Plain ", 6);
  ChapterTestAccess::startElement(&parser, "span", attributes);
  ChapterTestAccess::characterData(&parser, "Chapter", 7);
  ChapterTestAccess::endElement(&parser, "span");
  ChapterTestAccess::characterData(&parser, " after", 6);
  ChapterTestAccess::endElement(&parser, "p");

  ASSERT_EQ(ChapterTestAccess::paragraph(parser).size(), 0u);  // flushed to the page on </p>
  auto currentPage = ChapterTestAccess::takePage(parser);
  ASSERT_NE(currentPage, nullptr);
  const auto& block = currentPage->element(0).textBlock();
  ASSERT_EQ(block.wordCount(), 3);
  EXPECT_STREQ(block.wordText(1), "Chapter");
  EXPECT_EQ(block.wordStyle(0) & EpdFontFamily::SMALL_CAPS, 0);
  EXPECT_NE(block.wordStyle(1) & EpdFontFamily::SMALL_CAPS, 0);
  EXPECT_FALSE(block.wordHasDiscretionaryHyphen(1));  // shares the style byte's high bit
  EXPECT_EQ(block.wordStyle(2) & EpdFontFamily::SMALL_CAPS, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SmallCapsMetricsArePreparedForEachInlineFont) {
  renderer.sdFont = true;
  Paragraph text;
  const uint8_t slot = text.sizeSlotFor(0.8f);
  text.setSizeSlotFontId(slot, 42);
  text.addWord("Chapter", EpdFontFamily::SMALL_CAPS);
  text.addWord("other", static_cast<EpdFontFamily::Style>(EpdFontFamily::BOLD | EpdFontFamily::SMALL_CAPS), false,
               false, 8, 0, slot);
  size_t lines = 0;
  text.layoutAndExtractLines(renderer, 0, 480, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->wordCount(), 2);
    EXPECT_STREQ(line->wordText(0), "Chapter");
    EXPECT_STREQ(line->wordText(1), "other");
    EXPECT_EQ(line->wordFontId(1, 0), 42);
    EXPECT_NE(line->wordStyle(1) & EpdFontFamily::SMALL_CAPS, 0);
  });
  EXPECT_EQ(lines, 1u);
  EXPECT_EQ(renderer.coldMeasurements, 0u);
}

class ChapterPageBreakTest : public ChapterHtmlSlimParserTest {
 protected:
  unsigned pages = 0;
  void SetUp() override {
    ChapterHtmlSlimParserTest::SetUp();
    parser.completePageFn = [this](std::unique_ptr<Page>, uint16_t, uint16_t, uint32_t) { ++pages; };
    ASSERT_TRUE(parser.ensureSemantics());
    ChapterTestAccess::begin(parser);
  }
  void paragraph(const char* text, const char* style = nullptr) {
    const char* attributes[] = {"style", style, nullptr};
    ChapterTestAccess::startElement(&parser, "p", style ? attributes : nullptr);
    ChapterTestAccess::characterData(&parser, text, static_cast<int>(strlen(text)));
    ChapterTestAccess::endElement(&parser, "p");
  }
};

TEST_F(ChapterPageBreakTest, BreakBeforeStartsNewPage) {
  paragraph("one");
  paragraph("two");
  EXPECT_EQ(pages, 0u);
  paragraph("three", "page-break-before: always");
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterPageBreakTest, BreakAfterAppliesToFollowingContent) {
  paragraph("one", "break-after: page");
  EXPECT_EQ(pages, 0u);
  paragraph("two");
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterPageBreakTest, AvoidAndEmptyPageDoNotBreak) {
  paragraph("first", "page-break-before: always");
  paragraph("second", "page-break-before: avoid");
  EXPECT_EQ(pages, 0u);
}

TEST_F(ChapterPageBreakTest, ContainerBreaksOnceBeforeItsFirstChild) {
  paragraph("zero");
  const char* attributes[] = {"style", "page-break-before: always", nullptr};
  ChapterTestAccess::startElement(&parser, "div", attributes);
  paragraph("a");
  paragraph("b");
  ChapterTestAccess::endElement(&parser, "div");
  paragraph("c");
  EXPECT_EQ(pages, 1u);
}

class ChapterBlockDecorationTest : public ChapterHtmlSlimParserTest {
 protected:
  std::vector<std::unique_ptr<Page>> pages;
  void SetUp() override {
    ChapterHtmlSlimParserTest::SetUp();
    parser.completePageFn = [this](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
      pages.push_back(std::move(page));
    };
    ASSERT_TRUE(parser.ensureSemantics());
    ChapterTestAccess::begin(parser);
  }
  void open(const char* tag, const char* style) {
    const char* attributes[] = {"style", style, nullptr};
    ChapterTestAccess::startElement(&parser, tag, style ? attributes : nullptr);
  }
  void text(const std::string& value) {
    ChapterTestAccess::characterData(&parser, value.c_str(), static_cast<int>(value.size()));
  }
  void close(const char* tag) { ChapterTestAccess::endElement(&parser, tag); }
  static std::string words(int count) {
    std::string result;
    for (int i = 0; i < count; ++i) result += "word ";
    return result;
  }
  std::vector<PageElementView> elementsWithTag(PageElementTag tag) {
    std::vector<PageElementView> found;
    if (auto page = ChapterTestAccess::takePage(parser)) pages.push_back(std::move(page));
    for (const auto& page : pages) {
      for (uint16_t elementIndex = 0; elementIndex < page->elementCount(); ++elementIndex) {
        const auto element = page->element(elementIndex);
        if (element.getTag() == tag) found.push_back(element);
      }
    }
    return found;
  }
};

TEST_F(ChapterBlockDecorationTest, RepeatedSectionBreaksEachAddOneLine) {
  for (int breaks = 1; breaks <= 3; ++breaks) {
    open("p", "margin-top: 1px");
    text("Before");
    close("p");
    const int before = static_cast<int32_t>(ChapterTestAccess::assembly(parser, 1));
    for (int i = 0; i < breaks; ++i) {
      const char* attrs[] = {"class", "section-br", "style", "display: block", nullptr};
      ChapterTestAccess::startElement(&parser, "br", attrs);
      close("br");
    }
    EXPECT_EQ(static_cast<int32_t>(ChapterTestAccess::assembly(parser, 1)) - before,
              breaks * renderer.getLineHeight(0));
    open("p", "margin-top: 1px");
    text("After");
    close("p");
  }
}

TEST_F(ChapterBlockDecorationTest, InlineBreakDoesNotAddParagraphSpacing) {
  parser.extraParagraphSpacing = true;
  open("p", "margin-top: 1px; margin-bottom: 9px");
  text("First");
  open("br", nullptr);
  close("br");
  text("Second");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1].yPos - lines[0].yPos, renderer.getLineHeight(0));
}

TEST_F(ChapterBlockDecorationTest, LeadingBreakDoesNotAddBlankSpace) {
  open("br", nullptr);
  close("br");
  open("p", nullptr);
  text("Start");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].yPos, 0);
}

TEST_F(ChapterBlockDecorationTest, BreakAfterHeadingAddsOneLine) {
  open("h2", nullptr);
  text("Heading");
  close("h2");
  const int before = static_cast<int32_t>(ChapterTestAccess::assembly(parser, 1));
  open("br", nullptr);
  close("br");
  EXPECT_EQ(static_cast<int32_t>(ChapterTestAccess::assembly(parser, 1)) - before, renderer.getLineHeight(0));
}

TEST_F(ChapterBlockDecorationTest, PreWrapPreservesSpacesAndLineBreaksAcrossCallbacks) {
  parser.extraParagraphSpacing = true;
  open("p", "white-space: pre-wrap; text-align: justify");
  text("  first");
  text("  word\n");
  text("second");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  const auto& first = lines[0].textBlock();
  ASSERT_EQ(first.wordCount(), 6);
  EXPECT_STREQ(first.wordText(0), " ");
  EXPECT_STREQ(first.wordText(1), " ");
  EXPECT_STREQ(first.wordText(2), "first");
  EXPECT_STREQ(first.wordText(3), " ");
  EXPECT_STREQ(first.wordText(4), " ");
  EXPECT_STREQ(first.wordText(5), "word");
  // Literal spaces use the font's space advance, with no inserted word gaps.
  EXPECT_EQ(first.wordXpos(2) - first.wordXpos(0), 2 * renderer.getSpaceWidth(0, EpdFontFamily::REGULAR));
  EXPECT_EQ(lines[1].yPos - lines[0].yPos, renderer.getLineHeight(0));
}

TEST_F(ChapterBlockDecorationTest, BlockEmphasisIsInheritedOverriddenAndRestored) {
  open("blockquote", "font-style: italic");
  open("p", nullptr);
  text("Inherited");
  close("p");
  open("p", "font-style: normal");
  text("Normal");
  close("p");
  open("p", nullptr);
  text("Restored");
  close("p");
  close("blockquote");
  open("p", nullptr);
  text("Outside");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 4u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = lines[i].textBlock();
    EXPECT_EQ((block.wordStyle(0) & EpdFontFamily::ITALIC) != 0, i == 0 || i == 2);
  }
}

TEST_F(ChapterBlockDecorationTest, ExplicitFontInheritancePreservesInlineAndBlockEmphasis) {
  open("body", "font-weight:normal;font-style:normal");
  open("div", "font-weight:bold;font-style:italic");
  open("p", "font-weight:inherit;font-style:inherit");
  text("Block ");
  open("small", "font-weight:inherit;font-style:inherit");
  text("Small ");
  close("small");
  open("span", "font-weight:normal;font-style:normal");
  text("Normal ");
  open("span", "font-weight:inherit;font-style:inherit");
  text("Nested ");
  close("span");
  close("span");
  text("Restored");
  close("p");
  close("div");
  open("p", "font-weight:inherit;font-style:inherit");
  text("Outside");
  close("p");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  std::vector<std::pair<std::string, uint8_t>> actual;
  for (const auto element : lines) {
    const auto& block = element.textBlock();
    for (uint16_t i = 0; i < block.wordCount(); ++i) actual.emplace_back(block.wordText(i), block.wordStyle(i));
  }
  const uint8_t emphasis = EpdFontFamily::BOLD | EpdFontFamily::ITALIC;
  const std::vector<std::pair<std::string, uint8_t>> expected = {
      {"Block", emphasis}, {"Small", emphasis}, {"Normal", 0}, {"Nested", 0}, {"Restored", emphasis}, {"Outside", 0}};
  EXPECT_EQ(actual, expected);
}

TEST_F(ChapterBlockDecorationTest, InheritReplacesEarlierDeclarationsAndAllowsLaterOverrides) {
  open("body", "font-weight:normal;font-style:normal");
  open("div", "font-weight:bold;font-style:italic");
  open("p", "font-weight:normal;font-style:normal;font-weight:inherit;font-style:inherit");
  text("Inherited");
  close("p");
  open("p", "font-weight:inherit;font-style:inherit;font-weight:normal;font-style:normal");
  text("Normal");
  close("p");
  close("div");
  open("p", "font-weight:bold;font-style:italic;font-weight:inherit;font-style:inherit");
  text("ParentNormal");
  close("p");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = lines[i].textBlock();
    EXPECT_EQ(block.wordStyle(0), i == 0 ? EpdFontFamily::BOLD | EpdFontFamily::ITALIC : 0);
  }
}

TEST_F(ChapterBlockDecorationTest, InheritUsesSemanticParentEmphasisAndRestoresSiblingStyle) {
  open("p", nullptr);
  open("b", nullptr);
  open("i", nullptr);
  open("span", "font-weight:inherit;font-style:inherit");
  text("Inherited");
  close("span");
  close("i");
  close("b");
  text(" Outside");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto& block = lines[0].textBlock();
  ASSERT_EQ(block.wordCount(), 2u);
  EXPECT_EQ(block.wordStyle(0), EpdFontFamily::BOLD | EpdFontFamily::ITALIC);
  EXPECT_EQ(block.wordStyle(1), EpdFontFamily::REGULAR);
}

TEST_F(ChapterBlockDecorationTest, FontInheritanceUsesWinningCssRuleBeforeInlineOverride) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-font-inheritance.css";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const std::string css =
        "p{font-weight:bold;font-style:italic}"
        ".inherited{font-weight:normal;font-style:normal}"
        "#inherited{font-weight:inherit;font-style:inherit}";
    ASSERT_EQ(output.write(css.data(), css.size()), css.size());
  }
  {
    HalFile input;
    ASSERT_TRUE(input.open(path.c_str(), "rb"));
    ASSERT_EQ(cssParser.loadFromStream(input), CssParser::ParseResult::Complete);
  }
  std::filesystem::remove(path);
  open("body", "font-weight:normal;font-style:normal");
  const char* inherited[] = {"id", "inherited", "class", "inherited", nullptr};
  ChapterTestAccess::startElement(&parser, "p", inherited);
  text("Normal");
  close("p");
  open("div", "font-weight:bold;font-style:italic");
  ChapterTestAccess::startElement(&parser, "p", inherited);
  text("Emphasis");
  close("p");
  const char* overridden[] = {"id", "inherited", "style", "font-weight:normal;font-style:normal", nullptr};
  ChapterTestAccess::startElement(&parser, "p", overridden);
  text("Override");
  close("p");
  close("div");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = lines[i].textBlock();
    EXPECT_EQ(block.wordStyle(0), i == 1 ? EpdFontFamily::BOLD | EpdFontFamily::ITALIC : 0);
  }
}

TEST_F(ChapterBlockDecorationTest, PreElementPreservesNewlinesWithoutLeakingToFollowingParagraph) {
  open("pre", nullptr);
  text("one\ntwo");
  close("pre");
  open("p", nullptr);
  text("three\nfour");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  const auto& last = lines[2].textBlock();
  ASSERT_EQ(last.wordCount(), 2);
  EXPECT_STREQ(last.wordText(0), "three");
  EXPECT_STREQ(last.wordText(1), "four");
}

TEST_F(ChapterBlockDecorationTest, FloatedLeadingSpanBecomesDropCap) {
  open("p", nullptr);
  open("span", "float: left; font-size: 3em");
  text("T");
  close("span");
  text("he " + words(60));
  close("p");

  const auto caps = elementsWithTag(TAG_PageDropCap);
  ASSERT_EQ(caps.size(), 1u);
  EXPECT_STREQ(std::string(caps[0].dropCapText()).c_str(), "T");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(lines.size(), 4u);
  // Three lines wrap beside the letter, then text returns to the margin.
  EXPECT_GT(lines[0].xPos, 0);
  EXPECT_EQ(lines[2].xPos, lines[0].xPos);
  EXPECT_EQ(lines[3].xPos, 0);
  EXPECT_STREQ(lines[0].textBlock().wordText(0), "he");
}

TEST_F(ChapterBlockDecorationTest, LatinMultibyteDropCap) {
  open("p", nullptr);
  open("span", "float: left; font-size: 3em");
  text("É");
  close("span");
  text("he " + words(60));
  close("p");

  const auto caps = elementsWithTag(TAG_PageDropCap);
  ASSERT_EQ(caps.size(), 1u);
  EXPECT_STREQ(std::string(caps[0].dropCapText()).c_str(), "É");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(lines.size(), 4u);
  // Three lines wrap beside the letter, then text returns to the margin.
  EXPECT_GT(lines[0].xPos, 0);
  EXPECT_EQ(lines[2].xPos, lines[0].xPos);
  EXPECT_EQ(lines[3].xPos, 0);
  EXPECT_STREQ(lines[0].textBlock().wordText(0), "he");
}

TEST_F(ChapterBlockDecorationTest, HangulMultibyteDropCap) {
  open("p", nullptr);
  open("span", "float: left; font-size: 3em");
  text("한");
  close("span");
  text("he " + words(60));
  close("p");

  const auto caps = elementsWithTag(TAG_PageDropCap);
  ASSERT_EQ(caps.size(), 1u);
  EXPECT_STREQ(std::string(caps[0].dropCapText()).c_str(), "한");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(lines.size(), 4u);
  // Three lines wrap beside the letter, then text returns to the margin.
  EXPECT_GT(lines[0].xPos, 0);
  EXPECT_EQ(lines[2].xPos, lines[0].xPos);
  EXPECT_EQ(lines[3].xPos, 0);
  EXPECT_STREQ(lines[0].textBlock().wordText(0), "he");
}

TEST_F(ChapterBlockDecorationTest, DropCapPreservesDetectedDirectionAcrossExtraction) {
  open("p", nullptr);
  open("span", "float: left; font-size: 3em");
  text("א");
  close("span");
  text("אבג " + words(60));
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(lines.size(), 4u);
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = lines[i].textBlock();
    EXPECT_TRUE(block.getBlockStyle().isRtl) << "line " << i;
  }
}

TEST_F(ChapterBlockDecorationTest, MultiWordSpanStaysText) {
  open("p", nullptr);
  open("span", "float: left");
  text("Once upon");
  close("span");
  text(" a time");
  close("p");
  EXPECT_TRUE(elementsWithTag(TAG_PageDropCap).empty());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_STREQ(lines[0].textBlock().wordText(0), "Once");
}

TEST_F(ChapterBlockDecorationTest, FirstLetterRuleCapturesQuoteAndLetter) {
  const auto cssPath = std::filesystem::temp_directory_path() / "crosspoint-first-letter.css";
  {
    HalFile output;
    ASSERT_TRUE(output.open(cssPath.c_str(), "wb"));
    const std::string css = "p.opening::first-letter { initial-letter: 2; }";
    output.write(css.data(), css.size());
  }
  HalFile input;
  ASSERT_TRUE(input.open(cssPath.c_str(), "rb"));
  ASSERT_EQ(cssParser.loadFromStream(input), CssParser::ParseResult::Complete);

  const char* attributes[] = {"class", "opening", nullptr};
  ChapterTestAccess::startElement(&parser, "p", attributes);
  text("\xe2\x80\x9cIt was " + words(40));
  close("p");
  open("p", nullptr);
  text("Next paragraph");
  close("p");

  const auto caps = elementsWithTag(TAG_PageDropCap);
  ASSERT_EQ(caps.size(), 1u);
  EXPECT_STREQ(std::string(caps[0].dropCapText()).c_str(), "\xe2\x80\x9cI");
  std::filesystem::remove(cssPath);
}

TEST_F(ChapterBlockDecorationTest, BorderedBlockFramesItsTextAndInsetsIt) {
  open("div", "border: 2px solid black");
  open("p", nullptr);
  text("Framed text");
  close("p");
  close("div");
  open("p", nullptr);
  text("Outside");
  close("p");

  const auto boxes = elementsWithTag(TAG_PageBorderBox);
  ASSERT_EQ(boxes.size(), 1u);
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_GE(lines[0].xPos, 6);  // 2 px border + 4 px gap
  EXPECT_GT(lines[0].yPos, boxes[0].yPos);
  EXPECT_EQ(lines[1].xPos, 0);
}

TEST_F(ChapterBlockDecorationTest, EmptyBorderedBlockDrawsARule) {
  open("p", nullptr);
  text("Above");
  close("p");
  open("div", "border-top: 1px solid");
  close("div");
  open("p", nullptr);
  text("Below");
  close("p");
  const auto boxes = elementsWithTag(TAG_PageBorderBox);
  ASSERT_EQ(boxes.size(), 1u);
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_GT(boxes[0].yPos, lines[0].yPos);
  EXPECT_GT(lines[1].yPos, boxes[0].yPos);
}

TEST_F(ChapterBlockDecorationTest, ShadedBoxSplitsAcrossPages) {
  parser.viewportHeight = 100;
  open("div", "background-color: #ddd");
  for (int i = 0; i < 12; ++i) {
    open("p", nullptr);
    text("line");
    close("p");
  }
  close("div");
  EXPECT_GE(pages.size(), 1u);
  EXPECT_GE(elementsWithTag(TAG_PageBorderBox).size(), 2u);
}

class ChapterTableBorderTest : public ChapterBlockDecorationTest {
 protected:
  void table(const char* const* tableAttributes, const char* cellStyle) {
    ChapterTestAccess::startElement(&parser, "table", tableAttributes);
    ChapterTestAccess::startElement(&parser, "tr", nullptr);
    for (const char* cell : {"alpha", "beta"}) {
      open("td", cellStyle);
      text(cell);
      close("td");
    }
    close("tr");
    close("table");
  }
};

TEST_F(ChapterTableBorderTest, BorderAttributeFramesEveryCell) {
  const char* attributes[] = {"border", "1", nullptr};
  table(attributes, nullptr);
  EXPECT_EQ(elementsWithTag(TAG_PageBorderBox).size(), 2u);
  EXPECT_TRUE(elementsWithTag(TAG_PageHorizontalRule).empty());
}

TEST_F(ChapterTableBorderTest, CellCssBorderFramesEveryCell) {
  table(nullptr, "border: 1px solid black");
  const auto boxes = elementsWithTag(TAG_PageBorderBox);
  ASSERT_EQ(boxes.size(), 2u);
  EXPECT_EQ(boxes[0].yPos, boxes[1].yPos);
  EXPECT_LT(boxes[0].xPos, boxes[1].xPos);
}

TEST_F(ChapterTableBorderTest, UnborderedTableKeepsRowRule) {
  table(nullptr, nullptr);
  EXPECT_TRUE(elementsWithTag(TAG_PageBorderBox).empty());
  EXPECT_EQ(elementsWithTag(TAG_PageHorizontalRule).size(), 1u);
}

TEST_F(ChapterTableBorderTest, RichCellsPreserveParagraphStylesAndFollowingFlow) {
  parser.paragraphAlignment = static_cast<uint8_t>(CssTextAlign::None);  // Book's Style honors CSS alignment.
  const size_t initialDepth = xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_BLOCK_DEPTH);
  open("table", nullptr);
  open("tr", nullptr);
  open("td", nullptr);
  text("Firstcell");
  close("td");
  open("td", nullptr);
  open("p", "margin-left: 24px; font-style: italic; font-size: 0.75em");
  EXPECT_TRUE(ChapterTestAccess::assembly(parser, 2));
  EXPECT_TRUE(ChapterTestAccess::query(parser, 2));
  EXPECT_FLOAT_EQ(ChapterTestAccess::paragraph(parser).getBlockStyle().fontScale, 0.75f);
  text("Entryone");
  close("p");
  open("p", "text-align: right");
  EXPECT_FALSE(ChapterTestAccess::query(parser, 2));
  text("Entrytwo");
  close("p");
  close("td");
  close("tr");
  close("table");
  EXPECT_EQ(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_BLOCK_DEPTH), initialDepth);
  open("p", nullptr);
  text("Outside");
  close("p");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 4u);
  const char* expected[] = {"Firstcell", "Entryone", "Entrytwo", "Outside"};
  for (size_t i = 0; i < lines.size(); ++i) {
    EXPECT_STREQ(lines[i].textBlock().wordText(0), expected[i]);
    if (i > 0) EXPECT_GT(lines[i].yPos, lines[i - 1].yPos);
  }
  EXPECT_GE(lines[1].xPos, 24);
  const auto& left = lines[1].textBlock();
  const auto& right = lines[2].textBlock();
  EXPECT_GT(lines[2].xPos + right.wordXpos(0), lines[1].xPos + left.wordXpos(0));
  EXPECT_EQ(lines[3].xPos, 0);
}

TEST_F(ChapterTableBorderTest, CellImagesUseNormalImageHandlingAndRespectDisplayNone) {
  parser.imageRendering = 1;  // Exercise the shared image fallback without a decoder.
  open("table", nullptr);
  open("tr", nullptr);
  open("td", nullptr);
  const char* hidden[] = {"style", "display:none", "alt", "hidden", nullptr};
  ChapterTestAccess::startElement(&parser, "img", hidden);
  close("img");
  EXPECT_FALSE(ChapterTestAccess::assembly(parser, 2));
  const char* visible[] = {"alt", "skull", nullptr};
  ChapterTestAccess::startElement(&parser, "img", visible);
  close("img");
  EXPECT_TRUE(ChapterTestAccess::assembly(parser, 2));
  close("td");
  close("tr");
  close("table");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto block = lines[0].textBlock();
  ASSERT_EQ(block.wordCount(), 2u);
  EXPECT_STREQ(block.wordText(0), "[Image:");
  EXPECT_STREQ(block.wordText(1), "skull]");
}

class ChapterInlineSizeTest : public ChapterBlockDecorationTest {
 protected:
  void SetUp() override {
    ChapterBlockDecorationTest::SetUp();
    installBuiltinVariants(renderer);
    for (const int id : {NOTOSANS_12_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID}) {
      renderer.fontMap.emplace(id, EpdFontFamily(nullptr));
    }
    parser.fontId = NOTOSANS_14_FONT_ID;
  }
  int firstLineFontId() {
    const auto lines = elementsWithTag(TAG_PageLine);
    return lines.empty() ? -1 : lines[0].textBlock().getBlockStyle().fontId;
  }
};

TEST_F(ChapterInlineSizeTest, SpanHoldingWholeBlockSizesIt) {
  open("p", nullptr);
  open("span", "font-size: 2em");
  text("Chapter One");
  close("span");
  text(" ");
  close("p");
  EXPECT_EQ(firstLineFontId(), NOTOSANS_18_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, ShortSizedHeadingSpanFallsBackToStyledText) {
  open("h1", "font-size: 1em; font-style: italic");
  open("span", "font-size: 2em");
  text("II");
  close("span");
  close("h1");
  open("p", nullptr);
  text("After");
  close("p");

  EXPECT_TRUE(elementsWithTag(TAG_PageDropCap).empty());
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  const auto& heading = lines[0].textBlock();
  ASSERT_EQ(heading.wordCount(), 1u);
  EXPECT_STREQ(heading.wordText(0), "II");
  EXPECT_EQ(heading.getBlockStyle().fontId, NOTOSANS_18_FONT_ID);
  EXPECT_EQ(heading.wordStyle(0), EpdFontFamily::BOLD | EpdFontFamily::ITALIC);
  const auto& following = lines[1].textBlock();
  ASSERT_EQ(following.wordCount(), 1u);
  EXPECT_STREQ(following.wordText(0), "After");
  EXPECT_EQ(following.wordStyle(0), EpdFontFamily::REGULAR);
  EXPECT_EQ(following.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, NestedSpansCompoundTheirSizes) {
  open("p", nullptr);
  open("span", "font-size: 0.9em");
  open("span", "font-size: 0.9em");
  text("small print");
  close("span");
  close("span");
  close("p");
  EXPECT_EQ(firstLineFontId(), NOTOSANS_12_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, TextAfterTheSpanKeepsBodySize) {
  open("p", nullptr);
  open("span", "font-size: 2em");
  text("Lead");
  close("span");
  text(" and the rest of the paragraph");
  close("p");
  EXPECT_EQ(firstLineFontId(), 0);
  // The lead words keep their own size inline.
  const auto& block = elementsWithTag(TAG_PageLine)[0].textBlock();
  EXPECT_EQ(block.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_18_FONT_ID);
  EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, MixedSizesWithinALineSurviveTheCache) {
  open("p", nullptr);
  text("Normal ");
  open("span", "font-size: 0.8em");
  text("small");
  close("span");
  text(" then ");
  open("big", nullptr);
  text("big");
  close("big");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto& block = lines[0].textBlock();
  ASSERT_EQ(block.wordCount(), 4);
  EXPECT_EQ(block.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(block.wordFontId(3, NOTOSANS_14_FONT_ID), NOTOSANS_16_FONT_ID);

  const auto path = std::filesystem::temp_directory_path() / "crosspoint-word-fonts.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const uint8_t prefix[] = {1, 0, TAG_PageLine, 0, 0, 0, 0};
    ASSERT_EQ(output.write(prefix, sizeof(prefix)), sizeof(prefix));
    ASSERT_TRUE(block.serialize(output));
    const uint8_t footer[4]{};
    ASSERT_EQ(output.write(footer, sizeof(footer)), sizeof(footer));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  const auto cachedPage = Page::load(input, input.size());
  ASSERT_NE(cachedPage, nullptr);
  const auto cachedBlock = cachedPage->element(0).textBlock();
  const auto* cached = &cachedBlock;
  ASSERT_NE(cached, nullptr);
  for (uint16_t i = 0; i < block.wordCount(); ++i) {
    EXPECT_EQ(cached->wordFontId(i, NOTOSANS_14_FONT_ID), block.wordFontId(i, NOTOSANS_14_FONT_ID));
  }
  std::filesystem::remove(path);
}

TEST_F(ChapterBlockDecorationTest, SdAndVectorFontsSizeThroughTheVariantProvider) {
  constexpr int SD_FONT = 7;  // not a built-in ladder font
  parser.fontId = SD_FONT;
  installSyntheticVariants(renderer);
  open("h1", nullptr);
  text("Title");
  close("h1");
  open("p", nullptr);
  text("Body ");
  open("span", "font-size: 0.8em");
  text("small");
  close("span");
  close("p");

  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].textBlock().getBlockStyle().fontId, SD_FONT * 1000 + 200);
  const auto& body = lines[1].textBlock();
  EXPECT_EQ(body.wordFontId(0, SD_FONT), SD_FONT);
  EXPECT_EQ(body.wordFontId(1, SD_FONT), SD_FONT * 1000 + 80);
}

TEST_F(ChapterBlockDecorationTest, UnloadableVariantFallsBackToSectionFont) {
  EXPECT_EQ(TextBlock::renderFontId(renderer, 12345, 7), 7);
  installSyntheticVariants(renderer);
  EXPECT_EQ(TextBlock::renderFontId(renderer, 12345, 7), 12345);
}

TEST_F(ChapterInlineSizeTest, UniformLinesCarryNoWordFonts) {
  open("p", nullptr);
  text("Plain text only");
  close("p");
  const auto& block = elementsWithTag(TAG_PageLine)[0].textBlock();
  EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(block.extraAscent(renderer, NOTOSANS_14_FONT_ID), 0);
}

TEST_F(ChapterTableBorderTest, ScansColumnWidthsFromMarkup) {
  renderer.sdFont = true;
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-table-scan.xhtml";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const std::string html =
        "<table class=\"t\"><tr><th>ID</th><td>A much longer description of the item</td></tr>"
        "<tr><td colspan=\"2\">Spanning note that must not widen anything at all</td></tr>"
        "<tr><td>7</td><td>short<br/>lines &amp; more</td></tr></table><p>after</p>";
    output.write(html.data(), html.size());
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  ChapterHtmlSlimParser::TableColumnMeasure measure;
  ChapterTestAccess::state(parser);
  ASSERT_TRUE(parser.measureTableColumns(input, measure));
  EXPECT_EQ(renderer.coldMeasurements, 0u);
  EXPECT_EQ(measure.columns, 2);
  EXPECT_EQ(measure.min[0], 16);  // "ID"
  EXPECT_EQ(measure.min[1], 88);  // "description"
  EXPECT_EQ(measure.preferred[1], 31 * 8 + 6 * 4);

  xp_chapter_plan_columns(ChapterTestAccess::state(parser), &measure);
  ASSERT_EQ(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN_COUNT), 2);
  EXPECT_EQ(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN0 + 0) +
                xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN0 + 1),
            parser.viewportWidth);
  EXPECT_LT(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN0 + 0),
            xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN0 + 1));

  parser.viewportWidth = 200;  // too narrow for natural widths: column 0 keeps its word
  xp_chapter_plan_columns(ChapterTestAccess::state(parser), &measure);
  EXPECT_EQ(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN0 + 0), 16 + 8);
  EXPECT_EQ(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_COLUMN0 + 1), 200 - 24);
  std::filesystem::remove(path);
}

TEST_F(ChapterTableBorderTest, RowsUsePlannedColumnWidths) {
  ChapterTestAccess::startElement(&parser, "table", nullptr);
  ChapterTestAccess::columns(parser, 100, 380);
  ChapterTestAccess::startElement(&parser, "tr", nullptr);
  for (const char* cell : {"alpha", "beta"}) {
    open("td", nullptr);
    text(cell);
    close("td");
  }
  close("tr");
  close("table");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].xPos, 4);
  EXPECT_EQ(lines[1].xPos, 104);
}

TEST_F(ChapterTableBorderTest, CssCellPaddingInsetsContentAndReservesRowHeight) {
  open("table", nullptr);
  ChapterTestAccess::columns(parser, 100, 380);
  open("tr", nullptr);
  open("td", "padding: 9px 17px 13px 11px; text-align: right");
  text("alpha");
  close("td");
  open("td", "padding: 3px 0 5px");
  text("beta");
  close("td");
  close("tr");
  close("table");
  const int rowBottom = static_cast<int32_t>(ChapterTestAccess::assembly(parser, 1));
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].xPos, 11);
  EXPECT_EQ(lines[1].xPos, 100);
  EXPECT_EQ(lines[0].yPos, 9);
  EXPECT_EQ(lines[1].yPos, 3);
  const auto& first = lines[0].textBlock();
  EXPECT_EQ(lines[0].xPos + first.wordXpos(0) + renderer.getTextAdvanceX(0, "alpha", EpdFontFamily::REGULAR), 83);
  EXPECT_GE(rowBottom, 9 + renderer.getLineHeight(0) + 13);
}

TEST_F(ChapterTableBorderTest, PaddedRowsBreakBeforeTheBottomPaddingOverflows) {
  parser.viewportHeight = 48;
  table(nullptr, "padding: 8px");
  table(nullptr, "padding: 8px");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 4u);
  ASSERT_EQ(pages.size(), 2u);
  for (const auto line : lines) {
    EXPECT_EQ(line.yPos, 8);
    EXPECT_LE(line.yPos + renderer.getLineHeight(0) + 8, parser.viewportHeight);
  }
}

TEST_F(ChapterTableBorderTest, StackedCellPaddingSurroundsChildParagraphs) {
  const size_t initialDepth = xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_BLOCK_DEPTH);
  open("table", nullptr);
  open("tr", nullptr);
  open("td", "padding: 7px 8px 9px 10px");
  open("p", nullptr);
  text("Inside");
  close("p");
  close("td");
  close("tr");
  close("table");
  EXPECT_EQ(xp_chapter_query(ChapterTestAccess::state(parser), XP_CHAPTER_QUERY_BLOCK_DEPTH), initialDepth);
  const int rowBottom = static_cast<int32_t>(ChapterTestAccess::assembly(parser, 1));
  open("p", nullptr);
  text("Outside");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].xPos, 10);
  EXPECT_EQ(lines[0].yPos, 7);
  EXPECT_GE(rowBottom, 7 + renderer.getLineHeight(0) + 9);
  EXPECT_EQ(lines[1].xPos, 0);
}

TEST_F(ChapterTableBorderTest, LongTwoColumnFixtureKeepsColumnsAcrossPages) {
  parser.viewportWidth = 480;
  parser.viewportHeight = 64;
  const char* cells[] = {
      "This is one very long sentence. It is part of the first cell of this table. It is quite nice.",
      "This is the second cell. It will probably overflow into the next page. Lorem ipsum dolor sit amet, consetetur "
      "sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam "
      "voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata "
      "sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam "
      "nonumy eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et "
      "accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata sanctus est Lorem ipsum "
      "dolor sit amet."};
  std::multiset<std::string> expected[2];
  open("table", nullptr);
  ChapterTestAccess::columns(parser, 160, 320);
  open("tr", nullptr);
  for (size_t column = 0; column < 2; ++column) {
    open("td", "padding: 3px 6px");
    std::istringstream input(cells[column]);
    for (std::string word; input >> word;) expected[column].insert(word);
    // XML can split a cell across arbitrary character-data callbacks.
    const std::string content(cells[column]);
    for (size_t offset = 0; offset < content.size(); offset += 17) text(content.substr(offset, 17));
    close("td");
    EXPECT_FALSE(ChapterTestAccess::assembly(parser, 2));
  }
  close("tr");
  close("table");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_GT(pages.size(), 1u);
  std::multiset<std::string> actual[2];
  for (const auto element : lines) {
    const auto line = element;
    const auto block = line.textBlock();
    const size_t column = line.xPos == 6 ? 0 : 1;
    EXPECT_EQ(line.xPos, column == 0 ? 6 : 166);
    EXPECT_LE(line.yPos + renderer.getLineHeight(0), parser.viewportHeight);
    for (uint16_t i = 0; i < block.wordCount(); ++i) {
      actual[column].insert(block.wordText(i));
    }
  }
  EXPECT_EQ(actual[0], expected[0]);
  EXPECT_EQ(actual[1], expected[1]);
}

TEST_F(ChapterTableBorderTest, RowBudgetFallsBackAndResetsForTheNextRow) {
  open("table", nullptr);
  open("tr", nullptr);
  for (int column = 0; column < 2; ++column) {
    open("td", nullptr);
    text(words(100));
    close("td");
  }
  EXPECT_TRUE(ChapterTestAccess::assembly(parser, 2));
  close("tr");
  open("tr", nullptr);
  EXPECT_EQ(ChapterTestAccess::assembly(parser, 3), 0u);
  for (int column = 0; column < 2; ++column) {
    open("td", nullptr);
    text(words(70));
    close("td");
    EXPECT_FALSE(ChapterTestAccess::assembly(parser, 2));
  }
  close("tr");
  close("table");
  size_t count = 0;
  for (const auto element : elementsWithTag(TAG_PageLine)) {
    count += element.textBlock().wordCount();
  }
  EXPECT_EQ(count, 340u);
}

class ChapterWidowOrphanTest : public ChapterBlockDecorationTest {
 protected:
  void SetUp() override {
    ChapterBlockDecorationTest::SetUp();
    parser.viewportHeight = 5 * 16;  // five fixture lines per page
  }
  void paragraph(int wordCount) {
    open("p", nullptr);
    text(words(wordCount));
    close("p");
  }
  size_t linesOnPage(size_t index) {
    size_t count = 0;
    for (uint16_t i = 0; i < pages[index]->elementCount(); ++i)
      count += pages[index]->element(i).getTag() == TAG_PageLine;
    return count;
  }
};

TEST_F(ChapterWidowOrphanTest, LoneFirstLineMovesToNextPage) {
  paragraph(13 * 4);  // four full lines
  paragraph(13 * 3);  // its first line would be alone at the page bottom
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
}

TEST_F(ChapterWidowOrphanTest, LoneLastLineTakesALineAlong) {
  paragraph(13 * 5 + 3);  // six lines: the sixth would open page two alone
  paragraph(13);
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
  auto currentPage = ChapterTestAccess::takePage(parser);
  const auto& firstOnNext = currentPage->element(0);
  EXPECT_EQ(firstOnNext.yPos, 0);
}

TEST_F(ChapterWidowOrphanTest, HeadingMovesWithTheParagraphAfterIt) {
  paragraph(13 * 4);  // four lines
  open("h2", nullptr);
  text("Heading");
  close("h2");        // fits as the page's fifth line
  paragraph(13 * 3);  // cannot start on this page
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
  auto currentPage = ChapterTestAccess::takePage(parser);
  const auto& first = currentPage->element(0).textBlock();
  EXPECT_STREQ(first.wordText(0), "Heading");
}

TEST_F(ChapterWidowOrphanTest, HeadingStaysWhenFollowedOnItsPage) {
  paragraph(13 * 2);
  open("h2", nullptr);
  text("Heading");
  close("h2");
  paragraph(13 * 4);  // two of its four lines fit below the heading, two follow
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 5u);
}

TEST_F(ChapterWidowOrphanTest, TwoLineParagraphMovesWhole) {
  paragraph(13 * 4);
  paragraph(13 * 2);  // one line fits: moving just one would orphan, so both move
  ASSERT_GE(pages.size(), 1u);
  EXPECT_EQ(linesOnPage(0), 4u);
}

TEST_F(ChapterHtmlSlimParserTest, PassesIndentSettingsToNewTextBlock) {
  for (bool extraSpacing : {false, true}) {
    parser.extraParagraphSpacing = extraSpacing;
    ChapterTestAccess::assembly(parser, 101);
    parser.setParagraphIndentSpaces(5);
    ChapterTestAccess::assembly(parser, 111);
    ASSERT_NE(ChapterTestAccess::paragraph(parser).handle(), nullptr);
    EXPECT_EQ(xp_para_indent(ChapterTestAccess::paragraph(parser).handle()), 5);
  }
}

TEST_F(ChapterWidowOrphanTest, CarriedReferenceKeepsItsFootnote) {
  for (const bool repeated : {false, true}) {
    SCOPED_TRACE(repeated);
    pages.clear();
    ChapterTestAccess::assembly(parser, 105);
    paragraph(1);
    paragraph(1);
    ChapterTestAccess::assembly(parser, 107, 32);
    ChapterTestAccess::assembly(parser, 108, 2);
    ChapterTestAccess::assembly(parser, 109, 0);
    ChapterTestAccess::assembly(parser, 110, 10);
    ChapterTestAccess::addNote(parser, "1", "#note");
    ChapterTestAccess::addNote(parser, "2", "#other");
    ChapterTestAccess::addLink(parser, "#note", 0, 16, 8, 16);
    ChapterTestAccess::addLink(parser, "#other", 8, 0, 8, 16);
    if (repeated) ChapterTestAccess::addLink(parser, "#note", 0, 0, 8, 16);
    ASSERT_TRUE(ChapterTestAccess::assembly(parser, 8, 1));
    xp_chapter_test_carry(ChapterTestAccess::state(parser), 1, 1, 20);
    auto currentPage = ChapterTestAccess::takePage(parser);
    ASSERT_EQ(pages.size(), 1u);
    ASSERT_EQ(notes(*currentPage).size(), 1u);
    EXPECT_STREQ(notes(*currentPage)[0].href, "#note");
    EXPECT_STREQ(notes(*currentPage)[0].number, "1");
    EXPECT_EQ(notes(*pages[0]).size(), repeated ? 2u : 1u);
    const auto previousNotes = notes(*pages[0]);
    EXPECT_TRUE(std::any_of(previousNotes.begin(), previousNotes.end(),
                            [](const auto& note) { return strcmp(note.href, "#other") == 0; }));
    EXPECT_EQ(pageLinks(*currentPage)[0].y, 0);
  }
}

TEST_F(ChapterWidowOrphanTest, CarriedReferenceKeepsMultibyteFootnoteLabel) {
  for (const bool repeated : {false, true}) {
    SCOPED_TRACE(repeated);
    pages.clear();
    ChapterTestAccess::assembly(parser, 105);
    paragraph(1);
    paragraph(1);
    ChapterTestAccess::assembly(parser, 107, 32);
    ChapterTestAccess::assembly(parser, 108, 2);
    ChapterTestAccess::assembly(parser, 109, 0);
    ChapterTestAccess::assembly(parser, 110, 10);
    ChapterTestAccess::addNote(parser, "각주一", "#note");
    ChapterTestAccess::addNote(parser, "2", "#other");
    ChapterTestAccess::addLink(parser, "#note", 0, 16, 8, 16);
    ChapterTestAccess::addLink(parser, "#other", 8, 0, 8, 16);
    if (repeated) ChapterTestAccess::addLink(parser, "#note", 0, 0, 8, 16);
    ASSERT_TRUE(ChapterTestAccess::assembly(parser, 8, 1));
    xp_chapter_test_carry(ChapterTestAccess::state(parser), 1, 1, 20);
    auto currentPage = ChapterTestAccess::takePage(parser);
    ASSERT_EQ(pages.size(), 1u);
    ASSERT_EQ(notes(*currentPage).size(), 1u);
    EXPECT_STREQ(notes(*currentPage)[0].href, "#note");
    EXPECT_STREQ(notes(*currentPage)[0].number, "각주一");
    EXPECT_EQ(notes(*pages[0]).size(), repeated ? 2u : 1u);
    const auto previousNotes = notes(*pages[0]);
    EXPECT_TRUE(std::any_of(previousNotes.begin(), previousNotes.end(),
                            [](const auto& note) { return strcmp(note.href, "#other") == 0; }));
    EXPECT_EQ(pageLinks(*currentPage)[0].y, 0);
  }
}

TEST_F(ChapterPageBreakTest, TerminalBreakAfterDoesNotCreateEmptyPage) {
  paragraph("only", "page-break-after: always");
  ASSERT_TRUE(ChapterTestAccess::finish(parser));
  EXPECT_EQ(pages, 1u);
}

TEST_F(ChapterPageBreakTest, EmptyBlockDoesNotConsumePendingBreak) {
  paragraph("before", "page-break-after: always");
  paragraph("", "page-break-before: always");
  EXPECT_EQ(pages, 0u);
  paragraph("after");
  EXPECT_EQ(pages, 1u);
  ASSERT_TRUE(ChapterTestAccess::finish(parser));
  EXPECT_EQ(pages, 2u);
}

TEST_F(ChapterInlineSizeTest, NestedRemResetKeepsItsFontInsideSizedParagraph) {
  open("p", nullptr);
  open("span", "font-size: 0.8em");
  text("small ");
  open("span", "font-size: 1rem");
  text("normal");
  close("span");
  text(" small");
  close("span");
  close("p");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 1u);
  const auto& block = lines[0].textBlock();
  ASSERT_EQ(block.wordCount(), 3u);
  EXPECT_EQ(block.wordFontId(0, parser.fontId), NOTOSANS_12_FONT_ID);
  EXPECT_EQ(block.wordFontId(1, parser.fontId), NOTOSANS_14_FONT_ID);
  EXPECT_EQ(block.wordFontId(2, parser.fontId), NOTOSANS_12_FONT_ID);
}

TEST_F(ChapterInlineSizeTest, DescendantFontSizeResolvesAfterAncestorSize) {
  open("body", "font-size: 0.8em");
  open("p", "font-size: 1.2rem");
  text("Absolute");
  close("p");
  open("p", "font-size: 1.2em");
  text("Relative");
  close("p");
  open("p", nullptr);
  text("Inherited");
  close("p");
  close("body");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 3u);
  const int expected[] = {NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID};
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& block = lines[i].textBlock();
    EXPECT_EQ(block.wordFontId(0, NOTOSANS_14_FONT_ID), expected[i]);
  }
}

TEST_F(ChapterInlineSizeTest, SpecializedInlineTagsKeepTheirFontSize) {
  for (const char* tag : {"b", "strong", "i", "em", "u", "s", "sup", "sub", "a"}) {
    SCOPED_TRACE(tag);
    open("p", nullptr);
    text("Normal ");
    const char* attributes[] = {"style", "font-size: 0.8em", "href", "#note", nullptr};
    ChapterTestAccess::startElement(&parser, tag, attributes);
    text("small");
    close(tag);
    text(" after");
    close("p");
    const auto lines = elementsWithTag(TAG_PageLine);
    const auto& block = lines.back().textBlock();
    ASSERT_EQ(block.wordCount(), 3u);
    EXPECT_EQ(block.wordFontId(1, NOTOSANS_14_FONT_ID), NOTOSANS_12_FONT_ID);
    EXPECT_EQ(block.wordFontId(2, NOTOSANS_14_FONT_ID), NOTOSANS_14_FONT_ID);
  }
}

TEST_F(ChapterInlineSizeTest, EnlargedGridCellsReserveTheirHeightAcrossPages) {
  renderer.fontLineHeights[NOTOSANS_16_FONT_ID] = 32;
  renderer.fontAscenders[NOTOSANS_16_FONT_ID] = 24;
  parser.viewportHeight = 80;
  open("table", nullptr);
  for (const bool inlineSize : {false, true}) {
    open("tr", nullptr);
    open("td", inlineSize ? nullptr : "font-size: 1.2rem");
    if (inlineSize) open("span", "font-size: 1.2rem");
    text(words(18));
    if (inlineSize) close("span");
    close("td");
    open("td", nullptr);
    text("Normal");
    close("td");
    EXPECT_FALSE(ChapterTestAccess::assembly(parser, 2));
    close("tr");
  }
  close("table");
  ASSERT_TRUE(ChapterTestAccess::finish(parser));
  ASSERT_GE(pages.size(), 2u);
  size_t enlargedLines = 0;
  for (const auto& page : pages) {
    std::map<int, int> bottomByColumn;
    for (uint16_t elementIndex = 0; elementIndex < page->elementCount(); ++elementIndex) {
      const auto element = page->element(elementIndex);
      if (element.getTag() != TAG_PageLine) continue;
      const auto block = element.textBlock();
      const int lineFont = block.blockFontId(parser.fontId);
      const int height = renderer.getLineHeight(lineFont, parser.lineCompression) +
                         block.extraAscent(renderer, parser.fontId) +
                         block.getRubyShift(renderer.getFontAscenderSize(lineFont));
      EXPECT_GE(element.yPos, bottomByColumn[element.xPos]);
      EXPECT_LE(element.yPos + height, parser.viewportHeight);
      bottomByColumn[element.xPos] = element.yPos + height;
      if (element.xPos < parser.viewportWidth / 2) {
        ++enlargedLines;
        EXPECT_EQ(block.wordFontId(0, parser.fontId), NOTOSANS_16_FONT_ID);
      }
    }
  }
  EXPECT_GE(enlargedLines, 6u);
}

TEST_F(ChapterInlineSizeTest, GridCellsResolveBlockAndWordFonts) {
  open("table", nullptr);
  open("tr", nullptr);
  open("td", "font-size: 1.2rem");
  text("Large");
  close("td");
  open("td", nullptr);
  open("span", "font-size: 0.8em");
  text("Small");
  close("span");
  close("td");
  close("tr");
  close("table");
  const auto lines = elementsWithTag(TAG_PageLine);
  ASSERT_EQ(lines.size(), 2u);
  const auto& large = lines[0].textBlock();
  const auto& small = lines[1].textBlock();
  EXPECT_EQ(large.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_16_FONT_ID);
  EXPECT_EQ(small.wordFontId(0, NOTOSANS_14_FONT_ID), NOTOSANS_12_FONT_ID);
}

TEST_F(ChapterBlockDecorationTest, BorderScopeExcludesPrecedingParentText) {
  for (const char* tag : {"div", "h2"}) {
    SCOPED_TRACE(tag);
    open("div", nullptr);
    text("Outside");
    open(tag, "border: 1px solid");
    text("Inside");
    close(tag);
    close("div");
    const auto boxes = elementsWithTag(TAG_PageBorderBox);
    const auto lines = elementsWithTag(TAG_PageLine);
    ASSERT_GE(lines.size(), 2u);
    ASSERT_FALSE(boxes.empty());
    EXPECT_GT(boxes.back().yPos, lines[lines.size() - 2].yPos);
    EXPECT_LT(boxes.back().yPos, lines.back().yPos);
  }
}

TEST_F(ChapterBlockDecorationTest, AncestorShadePaintsBeforeNestedBorders) {
  open("div", "background: #eee");
  open("p", "border: 1px solid");
  text("Inside");
  close("p");
  close("div");
  auto currentPage = ChapterTestAccess::takePage(parser);
  ASSERT_NE(currentPage, nullptr);
  for (const bool placeholders : {false, true}) {
    renderer.boxPaints.clear();
    if (placeholders)
      currentPage->renderWithImagePlaceholders(renderer, 0, 0, 0);
    else
      currentPage->render(renderer, 0, 0, 0);
    ASSERT_FALSE(renderer.boxPaints.empty());
    EXPECT_EQ(renderer.boxPaints.front(), 's');
    EXPECT_NE(std::find(renderer.boxPaints.begin(), renderer.boxPaints.end(), 'b'), renderer.boxPaints.end());
    const auto firstBorder = std::find(renderer.boxPaints.begin(), renderer.boxPaints.end(), 'b');
    EXPECT_EQ(std::find(firstBorder, renderer.boxPaints.end(), 's'), renderer.boxPaints.end());
  }
}

}  // namespace

TEST(ParagraphIndentation, OverridesNonnegativeCssAndPreservesHangingIndent) {
  GfxRenderer renderer;
  for (int cssIndent : {-6, 0, 13}) {
    for (uint8_t spaces : {0, 1, 2, 5}) {
      BlockStyle style;
      style.alignment = CssTextAlign::Left;
      style.textIndentDefined = true;
      style.textIndent = cssIndent;
      Paragraph text(false, false, style, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      bool sawLine = false;
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        sawLine = true;
        EXPECT_EQ(line->wordXpos(0), cssIndent < 0 ? cssIndent : 4 * spaces);
      });
      EXPECT_TRUE(sawLine);
    }
  }
  for (uint8_t spaces : {0, 2}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    Paragraph text(false, false, style, spaces);
    text.addWord("word", EpdFontFamily::REGULAR);
    text.layoutAndExtractLines(
        renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 4 * spaces); });
  }
}

TEST(ParagraphIndentation, PreservesAlignmentEligibilityAndScaledSpaceRounding) {
  GfxRenderer renderer;
  for (const auto alignment : {CssTextAlign::Left, CssTextAlign::Center}) {
    for (uint8_t spaces : {0, 2}) {
      BlockStyle style;
      style.alignment = alignment;
      style.textIndentDefined = true;
      style.textIndent = 0;
      Paragraph text(false, false, style, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        if (alignment == CssTextAlign::Left)
          EXPECT_EQ(line->wordXpos(0), 4 * spaces);
        else
          EXPECT_EQ(line->wordXpos(0), 84);
      });
    }
  }
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  Paragraph text(false, false, style, 2);
  text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(
      renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 6); }, true, 0, 75);
}

TEST(ParagraphIndentation, ReducesOnlyFirstLineAvailableWidth) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  for (uint8_t spaces : {1, 2, 5}) {
    Paragraph text(false, false, style, spaces);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock>, auto) { ++lines; });
    EXPECT_EQ(lines, spaces == 1 ? 1u : 2u);
  }
}

TEST(ParagraphIndentation, PreviewKeyTracksOffAndWidths) {
  textsettings::PreviewKey off;
  EXPECT_EQ(off.paragraphIndentSpaces, 2);
  off.paragraphIndentSpaces = 0;
  auto on = off;
  on.paragraphIndentSpaces = 5;
  EXPECT_NE(off, on);
  on.paragraphIndentSpaces = 2;
  EXPECT_NE(off, on);
}

TEST(ParagraphIndentation, MigratesLegacySettingsAndClampsWidths) {
  EXPECT_EQ(migrateParagraphIndentSpaces(false, 0, true), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(false, 0, false), 2);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 0, false), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 2, true), 2);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 5, false), 5);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, -1, false), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 300, false), 5);
}

TEST(TextSpacingLayout, TrackingSeparatesCjkTokensAndScalesWordSpaces) {
  GfxRenderer renderer;
  for (bool hyphenation : {false, true}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    Paragraph text(hyphenation, false, style, 0);
    text.addWord("一二三", EpdFontFamily::REGULAR);
    text.addWord("四五", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(
        renderer, 0, 200,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ++lines;
          ASSERT_EQ(line->wordCount(), 5);
          EXPECT_EQ(line->wordXpos(0), 0);
          EXPECT_EQ(line->wordXpos(1), 7);  // 8 px glyph, -1 px tracking
          EXPECT_EQ(line->wordXpos(2), 14);
          EXPECT_EQ(line->wordXpos(3), 28);  // 8 px glyph plus 150% of a 4 px space, no tracking
          EXPECT_EQ(line->wordXpos(4), 35);
        },
        true, -1, 150);
    EXPECT_EQ(lines, 1u);
  }
  EXPECT_EQ(renderer.getTextAdvanceX(0, "ab", EpdFontFamily::REGULAR), 16);
  EXPECT_EQ(renderer.getSpaceWidth(0, EpdFontFamily::REGULAR), 4);
}

TEST(TextSpacingLayout, WordSpacingChangesWrapThreshold) {
  GfxRenderer renderer;
  for (uint8_t percent : {50, 100, 125, 200}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    Paragraph text(false, false, style, 0);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 36, [&](std::unique_ptr<TextBlock>, auto) { ++lines; }, true, 0, percent);
    EXPECT_EQ(lines, percent > 100 ? 2u : 1u);  // 16 + 16 + scaled 4 px space
  }
}

TEST(TextSpacingLayout, CachedPageRestoresSpacing) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  Paragraph text(false, false, style);
  text.addWord("一二三", EpdFontFamily::REGULAR);
  text.addWord("四五", EpdFontFamily::REGULAR);
  const auto path = (std::filesystem::temp_directory_path() / "crosspoint-text-spacing.bin").string();
  unsigned lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 200,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        Page page;
        page.elements.push_back(std::make_unique<PageLine>(std::move(line), 4, 12));
        const auto* original = static_cast<const PageLine&>(*page.elements[0]).getBlock();
        {
          HalFile file;
          ASSERT_TRUE(file.open(path.c_str(), "wb"));
          ASSERT_TRUE(page.serialize(file));
        }
        HalFile file;
        ASSERT_TRUE(file.open(path.c_str(), "rb"));
        auto cachedPage = Page::load(file, file.size() - file.position());
        ASSERT_NE(cachedPage, nullptr);
        ASSERT_EQ(cachedPage->elementCount(), 1);
        const auto cachedBlock = cachedPage->element(0).textBlock();
        const auto* cached = &cachedBlock;
        ASSERT_NE(cached, nullptr);
        EXPECT_EQ(cached->getBlockStyle().characterSpacing, -2);
        ASSERT_EQ(cached->wordCount(), 5);
        EXPECT_EQ(cached->wordXpos(3) - cached->wordXpos(2), 10);  // 8 + half-width space
        EXPECT_EQ(file.position(), file.size());
        ASSERT_EQ(cached->wordCount(), original->wordCount());
        for (uint16_t i = 0; i < original->wordCount(); ++i) EXPECT_EQ(cached->wordXpos(i), original->wordXpos(i));
      },
      true, -2, 50);
  EXPECT_EQ(lines, 1u);
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ParserAppliesTextSpacingToParagraphs) {
  parser.setTextSpacing(-1, 150);
  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "p", nullptr);
  const std::string text = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89 \xe5\x9b\x9b\xe4\xba\x94";  // 一二三 四五
  ChapterTestAccess::characterData(&parser, text.c_str(), static_cast<int>(text.size()));
  ChapterTestAccess::endElement(&parser, "p");
  ChapterTestAccess::assembly(parser, 102);
  auto currentPage = ChapterTestAccess::takePage(parser);
  ASSERT_NE(currentPage, nullptr);
  unsigned lines = 0;
  for (uint16_t elementIndex = 0; elementIndex < currentPage->elementCount(); ++elementIndex) {
    const auto element = currentPage->element(elementIndex);
    if (element.getTag() != TAG_PageLine) continue;
    const auto& block = element.textBlock();
    ++lines;
    ASSERT_EQ(block.wordCount(), 5);
    EXPECT_EQ(block.getBlockStyle().characterSpacing, -1);
    EXPECT_EQ(block.wordXpos(1) - block.wordXpos(0), 7);   // 8 px glyph, -1 px tracking
    EXPECT_EQ(block.wordXpos(3) - block.wordXpos(2), 14);  // glyph plus 150% of a 4 px space
  }
  EXPECT_EQ(lines, 1u);
}

TEST_F(ChapterHtmlSlimParserTest, NfdWordAcrossInlineStyleKeepsSourceRange) {
  ASSERT_TRUE(parser.ensureSemantics());
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "body", nullptr);
  ChapterTestAccess::startElement(&parser, "p", nullptr);
  const std::string prefix = "Cafe\xCC\x81";
  ChapterTestAccess::characterData(&parser, prefix.c_str(), static_cast<int>(prefix.size()));
  ChapterTestAccess::startElement(&parser, "b", nullptr);
  ChapterTestAccess::characterData(&parser, "s", 1);
  ChapterTestAccess::endElement(&parser, "b");
  ChapterTestAccess::endElement(&parser, "p");
  ChapterTestAccess::assembly(parser, 102);
  auto currentPage = ChapterTestAccess::takePage(parser);
  ASSERT_NE(currentPage, nullptr);
  std::string selected;
  uint32_t previousEnd = 0;
  unsigned words = 0;
  for (uint16_t elementIndex = 0; elementIndex < currentPage->elementCount(); ++elementIndex) {
    const auto element = currentPage->element(elementIndex);
    if (element.getTag() != TAG_PageLine) continue;
    const auto& block = element.textBlock();
    for (uint16_t i = 0; i < block.wordCount(); ++i) {
      const auto range = block.wordSourceRange(i);
      EXPECT_EQ(range.start, words == 0 ? 0u : 5u);
      EXPECT_EQ(range.end, words == 0 ? 5u : 6u);
      ASSERT_TRUE(clippingText::append(selected, block.wordText(i), range.start > previousEnd ? ' ' : '\0', 4096,
                                       block.wordHasDiscretionaryHyphen(i)));
      previousEnd = range.end;
      ++words;
    }
  }
  EXPECT_EQ(words, 2u);
  EXPECT_EQ(selected, "Caf\xC3\xA9s");
}

class ClippingParagraphTest : public ChapterHtmlSlimParserTest {};

TEST_P(ClippingParagraphTest, CachedBlocksSeparateAdjacentParagraphs) {
  EXPECT_EQ(parseToParagraphText(GetParam()), "one\ntwo");
}

TEST_F(ChapterHtmlSlimParserTest, TextBetweenBlocksStaysInItsOwnParagraph) {
  EXPECT_EQ(parseToParagraphText("<p>one</p>two<div>three</div>"), "one\ntwo\nthree");
}

INSTANTIATE_TEST_SUITE_P(ClippingBoundaries, ClippingParagraphTest,
                         ::testing::Values("<p>one</p><p>two</p>", "<p>one</p>\n  <p>two</p>",
                                           "<div>one</div><div>two</div>", "<div>one</div>\n  <div>two</div>",
                                           "<p>one<br/>two</p>", "<p>o<b>ne</b></p><p>two</p>",
                                           "<p>one</p><p>&#160;two</p>", "<p>one</p><p>&#8239;two</p>",
                                           "one<div>two</div>", "<div>one<div>two</div></div>",
                                           "<div>one<p>two</p></div>", "<div>one<h3>two</h3></div>"));

TEST(TextSpacingLayout, ParagraphMarkerSurvivesBidiAndOnlyMarksFirstExtractedLine) {
  GfxRenderer renderer;
  for (const bool focus : {false, true}) {
    BlockStyle style;
    style.isRtl = true;
    style.directionDefined = true;
    style.alignment = CssTextAlign::Right;
    Paragraph text(false, focus, style, 0);
    text.addWord("אחד", EpdFontFamily::REGULAR);
    text.addWord("alpha", EpdFontFamily::REGULAR);
    text.addWord("beta", EpdFontFamily::REGULAR);
    text.addWord("שני", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    unsigned starts = 0;
    auto inspect = [&](std::unique_ptr<TextBlock> block, auto) {
      for (uint16_t i = 0; i < block->wordCount(); ++i) {
        if (!block->wordStartsParagraph(i)) continue;
        EXPECT_EQ(lines, 0u);
        EXPECT_STREQ(block->wordText(i), "אחד");
        ++starts;
      }
      ++lines;
    };
    text.layoutAndExtractLines(renderer, 0, 100, inspect, false);
    ASSERT_GT(lines, 0u);
    ASSERT_GT(text.size(), 0u);
    text.layoutAndExtractLines(renderer, 0, 100, inspect);
    EXPECT_EQ(starts, 1u);
    EXPECT_GT(lines, 1u);
  }
}

TEST(KoreanLayout, HangulWordsStayWholeAndWrapAtSpaces) {
  GfxRenderer renderer;
  {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    Paragraph text(false, false, style, 0);
    text.addWord("가나다", EpdFontFamily::REGULAR);
    text.addWord("라마", EpdFontFamily::REGULAR);
    text.addWord("3개를", EpdFontFamily::REGULAR);
    text.addWord("iPhone을", EpdFontFamily::REGULAR);
    std::vector<std::vector<std::string>> lines;
    text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
      auto& words = lines.emplace_back();
      for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
    });
    // 가나다 라마 is 24 + 4 + 16 px; adding 3개를 would need 72 px, and no break exists inside it.
    const std::vector<std::vector<std::string>> expected{{"가나다", "라마"}, {"3개를"}, {"iPhone을"}};
    EXPECT_EQ(lines, expected);
  }
}

TEST(KoreanLayout, JustifiedHangulStretchesOnlyWordSpaces) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  Paragraph text(false, false, style, 0);
  for (const char* word : {"가나", "다라", "마바", "사아"}) text.addWord(word, EpdFontFamily::REGULAR);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
    if (lines++ != 0) return;
    // 3 x 16 px words + 2 x 4 px spaces leave 4 px, split across the two spaces only.
    ASSERT_EQ(line->wordCount(), 3);
    EXPECT_EQ(line->wordXpos(0), 0);
    EXPECT_EQ(line->wordXpos(1), 22);
    EXPECT_EQ(line->wordXpos(2), 44);
  });
  EXPECT_EQ(lines, 2u);
}

TEST(KoreanLayout, HangulGluedAcrossInlineStyleIsUnbreakable) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  Paragraph text(false, false, style);
  text.addWord("가나", EpdFontFamily::REGULAR);
  text.addWord("한국", EpdFontFamily::REGULAR);
  text.addWord("어", EpdFontFamily::BOLD, false, /*attachToPrevious=*/true);
  std::vector<std::vector<std::string>> lines;
  text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock> line, auto) {
    auto& words = lines.emplace_back();
    for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
  });
  // 가나 한국 fits in 36 px, but 어 is glued to 한국, so the whole word moves down.
  const std::vector<std::vector<std::string>> expected{{"가나"}, {"한국", "어"}};
  EXPECT_EQ(lines, expected);
}

TEST(TextDecorationPosition, StrikeUsesGlyphBodyAndFollowsScriptScaling) {
  GfxRenderer renderer;
  constexpr int top = 20;
  for (const auto script : {EpdFontFamily::REGULAR, EpdFontFamily::SUP, EpdFontFamily::SUB}) {
    Paragraph text(false, false, BlockStyle{}, 0);
    text.addWord("example", static_cast<EpdFontFamily::Style>(EpdFontFamily::STRIKETHROUGH | script));
    text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, uint32_t) {
      renderer.drawnLineCount = 0;
      line->render(renderer, 0, 0, top);
      ASSERT_EQ(renderer.drawnLineCount, 1);
      // Fixture baseline is top + 12; glyph body is 10 pixels, halved for scripts.
      const int expected = script == EpdFontFamily::SUP ? 26 : script == EpdFontFamily::SUB ? 33 : 27;
      EXPECT_EQ(renderer.lastLineY, expected);
    });
  }
}

TEST(TextDecorationPosition, UnderlineStaysBelowBaseline) {
  GfxRenderer renderer;
  Paragraph text(false, false, BlockStyle{}, 0);
  text.addWord("example", EpdFontFamily::UNDERLINE);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, uint32_t) {
    line->render(renderer, 0, 0, 20);
    EXPECT_EQ(renderer.lastLineY, 34);
  });
}

TEST(ParagraphStorageAbi, WordAllocationFailureLeavesOutputUnchanged) {
  alignas(void*) uint8_t state[4 * sizeof(void*)];
  const XpParaAllocator allocator = {[](size_t) -> void* { return nullptr; }, [](void*) {}};
  auto* store = reinterpret_cast<XpWordStore*>(state);
  xp_word_store_init(store, &allocator);
  XpStoredWord output{123, 45, 67};
  EXPECT_EQ(xp_word_store_append(store, reinterpret_cast<const uint8_t*>("word"), 4, &output), 0);
  EXPECT_EQ(output.chunk, 123u);
  EXPECT_EQ(output.off, 45u);
  EXPECT_EQ(output.len, 67u);
  EXPECT_EQ(xp_word_store_chunk_count(store), 0u);
  xp_word_store_destroy(store);
}

TEST(ParagraphStorageAbi, MetadataAllocationFailureLeavesSequenceEmpty) {
  alignas(void*) uint8_t state[26 * sizeof(void*)];
  const XpParaAllocator allocator = {[](size_t) -> void* { return nullptr; }, [](void*) {}};
  auto* tokens = reinterpret_cast<XpTokens*>(state);
  xp_tokens_init(tokens, &allocator);
  const XpToken input{{0, 0, 4}, 1, 2, 3, 4, 3};
  EXPECT_EQ(xp_tokens_insert(tokens, 0, &input, 70000), 0);
  EXPECT_EQ(xp_tokens_size(tokens), 0u);
  EXPECT_EQ(xp_tokens_record_absorbed(tokens, 70001), 0);
  EXPECT_EQ(xp_tokens_source_after(tokens, 70000, 4), 70004u);
  xp_tokens_destroy(tokens);
}

extern "C" void xp_test_fail_array_bytes(size_t);
extern "C" size_t xp_test_array_failures();

extern "C" size_t xp_test_live_bytes();
namespace {
struct CompletionAudit {
  size_t failAt = SIZE_MAX;
  size_t attempts = 0;
  size_t created[2]{};
  size_t destroyed[2]{};
  std::map<const void*, BlockType> payloads;
  size_t beforeElements = 0, afterElements = 0, beforeLinks = 0, beforeNotes = 0, beforeComplete = 0;
  uint32_t elements = 0, links = 0, notes = 0;
  XpChapterSink original{};
} completionAudit;
XpChapterSink auditedSink(const XpChapterSink& original) {
  completionAudit.original = original;
  auto sink = original;
  sink.complete = [](void* ctx, XpChapterSpan page, XpChapterPosition position) -> uint8_t {
    XpPageInfo info{};
    EXPECT_EQ(xp_page_arena_size(page.data, page.len, &info), 0u);
    EXPECT_EQ(info.consumed, page.len);
    completionAudit.elements = info.elements;
    completionAudit.links = info.links;
    completionAudit.notes = info.footnotes;
    completionAudit.beforeComplete = xp_test_live_bytes();
    return completionAudit.original.complete(ctx, page, position);
  };
  return sink;
}
}  // namespace
bool chapterTestFailPageAllocation() { return completionAudit.attempts++ == completionAudit.failAt; }
void chapterTestBlockDestroyed(const void* raw) {
  const auto found = completionAudit.payloads.find(raw);
  if (found == completionAudit.payloads.end()) return;
  ++completionAudit.destroyed[found->second];
  completionAudit.payloads.erase(found);
}

TEST_F(ChapterHtmlSlimParserTest, CompletionPassesOneCompletePageWithMetadata) {
  completionAudit = {};
  auto sink = auditedSink(parser.sink_);
  const auto options = parser.chapterOptions();
  ASSERT_EQ(xp_chapter_create(&parser.chapter_, &sink, &parser, cssParser.rustHandle(), &options), 0);
  ChapterTestAccess::begin(parser);
  ChapterTestAccess::startElement(&parser, "p", nullptr);
  ChapterTestAccess::characterData(&parser, "text", 4);
  ChapterTestAccess::endElement(&parser, "p");
  for (size_t i = 0; i < Page::MAX_LINKS_PER_PAGE; ++i)
    ChapterTestAccess::addLink(parser, ("#link" + std::to_string(i)).c_str(), 0, 0, 8, 16);
  for (size_t i = 0; i < Page::MAX_FOOTNOTES_PER_PAGE; ++i)
    ChapterTestAccess::addNote(parser, std::to_string(i).c_str(), ("#note" + std::to_string(i)).c_str());
  size_t pages = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    EXPECT_EQ(page->linkCount(), 32u);
    EXPECT_EQ(page->footnoteCount(), 16u);
    EXPECT_EQ(xp_test_live_bytes(), completionAudit.beforeComplete);
  };
  ChapterTestAccess::assembly(parser, 104);
  EXPECT_EQ(pages, 1u);
  EXPECT_EQ(completionAudit.links, 32u);
  EXPECT_EQ(completionAudit.notes, 16u);
  parser.abortParse();
  EXPECT_TRUE(completionAudit.payloads.empty());
  EXPECT_EQ(completionAudit.created[TEXT_BLOCK], completionAudit.destroyed[TEXT_BLOCK]);
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}

TEST_F(ChapterHtmlSlimParserTest, CompletionObjectFailuresConsumeEveryPayloadExactlyOnce) {
  const std::string html =
      "<html><body><div style='border:1px solid'><p><span style='float:left;font-size:3em'>T</span>"
      "ext one two three four five six seven eight nine ten</p><hr/><img src='probe.gif'/></div>";
  Epub::enableGifProbeForTest = true;
  auto book = std::make_shared<Epub>();
  book->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 8, 0, 8, 0};
  size_t required = 0;
  for (size_t iteration = 0; iteration == 0 || iteration <= required; ++iteration) {
    SCOPED_TRACE(iteration);
    completionAudit = {};
    completionAudit.failAt = iteration ? iteration - 1 : SIZE_MAX;
    ChapterHtmlSlimParser candidate{book,  filepath, renderer, 0,  1.0f, false, 0,  480,     800,       false,
                                    false, {},       true,     "", "",   0,     {}, nullptr, &cssParser};
    auto sink = auditedSink(candidate.sink_);
    const auto options = candidate.chapterOptions();
    ASSERT_EQ(xp_chapter_create(&candidate.chapter_, &sink, &candidate, cssParser.rustHandle(), &options), 0);
    size_t pages = 0;
    candidate.completePageFn = [&](std::unique_ptr<Page>, auto, auto, auto) { ++pages; };
    xp_chapter_begin(candidate.chapter_);
    const bool fed =
        xp_chapter_feed(candidate.chapter_, reinterpret_cast<const uint8_t*>(html.data()), html.size(), 0) != 2;
    for (size_t i = 0; i < 32; ++i)
      ChapterTestAccess::addLink(candidate, ("#link" + std::to_string(i)).c_str(), 0, 0, 8, 16);
    for (size_t i = 0; i < 16; ++i)
      ChapterTestAccess::addNote(candidate, std::to_string(i).c_str(), ("#note" + std::to_string(i)).c_str());
    const std::string end = "</body></html>";
    const bool ended =
        xp_chapter_feed(candidate.chapter_, reinterpret_cast<const uint8_t*>(end.data()), end.size(), 1) == 1;
    const bool done = fed && ended && xp_chapter_finish(candidate.chapter_);
    if (iteration == 0) required = completionAudit.attempts;
    EXPECT_EQ(done, iteration == 0);
    EXPECT_EQ(pages, iteration == 0 ? 1u : 0u);
    candidate.abortParse();
    EXPECT_EQ(completionAudit.created[TEXT_BLOCK], completionAudit.destroyed[TEXT_BLOCK]);
    EXPECT_EQ(completionAudit.created[IMAGE_BLOCK], completionAudit.destroyed[IMAGE_BLOCK]);
    EXPECT_TRUE(completionAudit.payloads.empty());
    EXPECT_EQ(xp_test_live_allocations(), 0u);
  }
  EXPECT_EQ(required, 1u);  // One page arena replaces the object reconstruction.
  completionAudit = {};
}

TEST_F(ChapterHtmlSlimParserTest, ByteSinkFailureRejectsChapter) {
  auto sink = parser.sink_;
  sink.complete = [](void*, XpChapterSpan bytes, XpChapterPosition) -> uint8_t {
    EXPECT_GT(bytes.len, 6u);
    return 2;
  };
  const auto options = parser.chapterOptions();
  ASSERT_EQ(xp_chapter_create(&parser.chapter_, &sink, &parser, cssParser.rustHandle(), &options), 0);
  ASSERT_TRUE(xp_chapter_begin(parser.chapter_));
  const std::string html = "<html><body><p><a href='#one'>one</a></p></body></html>";
  xp_chapter_feed(parser.chapter_, reinterpret_cast<const uint8_t*>(html.data()), html.size(), 1);
  EXPECT_FALSE(xp_chapter_finish(parser.chapter_));
  parser.abortParse();
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}

TEST(ParagraphLinks, ManyLinksUseCheckedDestinationAndPropagateFailure) {
  GfxRenderer renderer;
  for (const bool fail : {false, true}) {
    Paragraph text(false, false, BlockStyle{}, 0);
    for (size_t i = 0; i < 255; ++i) {
      const std::string href = "#" + std::to_string(i);
      const auto id = text.addLinkTarget(href.c_str());
      ASSERT_NE(id, 0);
      text.addWord("a", EpdFontFamily::REGULAR, false, false, i, id);
    }
    size_t lines = 0;
    xp_test_fail_array_bytes(fail ? 255 * sizeof(TextBlock::LinkSpan) : 0);
    text.layoutAndExtractLines(renderer, 0, 4096, [&](std::unique_ptr<TextBlock> line, auto) {
      ++lines;
      auto links = line->takeLinkSpans();
      ASSERT_EQ(links.count, 255u);
      EXPECT_EQ(line->takeLinkSpans().count, 0u);
      for (size_t i = 0; i < links.count; ++i) EXPECT_EQ(links.data[i].href, "#" + std::to_string(i));
    });
    const size_t failures = xp_test_array_failures();
    xp_test_fail_array_bytes(0);
    EXPECT_EQ(lines, fail ? 0u : 1u);
    EXPECT_EQ(text.hadDroppedWords(), fail);
    EXPECT_EQ(failures, fail ? 1u : 0u);
  }
}

TEST_F(ChapterHtmlSlimParserTest, LongSingleRubyElementKeepsAllAnnotations) {
  std::string html = "<html><body><p><ruby>";
  for (size_t i = 0; i < 2049; ++i) html += "a<rt>r</rt>";
  html += "</ruby></p></body></html>";
  {
    HalFile file;
    ASSERT_TRUE(file.open(filepath.c_str(), "wb"));
    ASSERT_EQ(file.write(html.data(), html.size()), html.size());
  }
  size_t words = 0;
  parser.completePageFn = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    for (uint16_t elementIndex = 0; elementIndex < page->elementCount(); ++elementIndex) {
      const auto element = page->element(elementIndex);
      if (element.getTag() != TAG_PageLine) continue;
      const auto& block = element.textBlock();
      ASSERT_EQ(block.getRubyTexts().size(), block.wordCount());
      for (uint16_t i = 0; i < block.wordCount(); ++i) {
        EXPECT_STREQ(block.wordText(i), "a");
        EXPECT_EQ(block.getRubyTexts()[i], "r");
        ++words;
      }
    }
  };
  EXPECT_TRUE(parser.parseAndBuildPages());
  EXPECT_EQ(words, 2049u);
  std::filesystem::remove(filepath);
}

TEST_F(ChapterHtmlSlimParserTest, MultilingualImageFallbackMatchesCppCacheText) {
  parser.epub = std::make_shared<Epub>();
  Epub::enableGifProbeForTest = true;
  for (const char* source : {"unsupported.svg", "failed.gif"}) {
    for (const char* alt : {"한국어", "עברית"}) {
      SCOPED_TRACE(source);
      SCOPED_TRACE(alt);
      const std::string body = "<p><img src='" + std::string(source) + "' alt='" + alt + "'/></p>";
      // Captured from c4db5201, including synthetic-source gaps and visual RTL order.
      const char* expected = strcmp(alt, "한국어") == 0 ? "[Image:한국어]" : "עברית]\n[Image:";
      EXPECT_EQ(parseToParagraphText(body), expected);
    }
  }
}

TEST_F(ChapterHtmlSlimParserTest, EncodedLineLinksSurviveCarryAndReleaseAtAbort) {
  parser.viewportHeight = 32;
  size_t pages = 0, links = 0;
  parser.setPageBytesCallback([&](XpChapterSpan bytes, auto, auto, auto) {
    XpPageInfo info{};
    ASSERT_EQ(xp_page_arena_size(bytes.data, bytes.len, &info), 0u);
    ++pages;
    links += info.links;
  });
  const auto options = parser.chapterOptions();
  ASSERT_EQ(xp_chapter_create(&parser.chapter_, &parser.sink_, &parser, cssParser.rustHandle(), &options), 0);
  ASSERT_TRUE(xp_chapter_begin(parser.chapter_));
  const std::string html =
      "<html><body><p><a href='#one'>one</a></p><p><a href='#two'>two</a></p><p><a "
      "href='#three'>three</a></p></body></html>";
  ASSERT_NE(xp_chapter_feed(parser.chapter_, reinterpret_cast<const uint8_t*>(html.data()), html.size(), 1), 2u);
  EXPECT_TRUE(xp_chapter_finish(parser.chapter_));
  EXPECT_GE(pages, 2u);
  EXPECT_EQ(links, 3u);
  parser.abortParse();
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}
