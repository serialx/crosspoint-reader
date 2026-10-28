#include <Epub.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <MemoryManager.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include <xp_ffi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();
extern "C" XpParagraph* xp_chapter_test_paragraph(XpChapterState*);
extern "C" uint32_t xp_chapter_test_assembly(XpChapterState*, uint32_t, uint32_t);
extern "C" void xp_chapter_test_event(XpChapterState*, uint32_t, const XpChapterSpan*, uint32_t);
class ChapterDiagnostics : public ::testing::Test {
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
  void TearDown() override { Epub::enableGifProbeForTest = false; }

 protected:
  void SetUp() override { ChapterLogs::lines.clear(); }
  bool has(const std::string& line) {
    return std::find(ChapterLogs::lines.begin(), ChapterLogs::lines.end(), line) != ChapterLogs::lines.end();
  }
  bool parse(const std::string& html) {
    filepath = (std::filesystem::temp_directory_path() / ("chapter-diagnostics-" + std::to_string(getpid()) + ".xhtml"))
                   .string();
    {
      std::ofstream output(filepath);
      output << html;
    }
    parser.completePageFn = [](std::unique_ptr<Page>, auto, auto, auto) {};
    const bool ok = parser.parseAndBuildPages();
    parser.abortParse();
    std::filesystem::remove(filepath);
    return ok;
  }
};
TEST_F(ChapterDiagnostics, XmlErrors) {
  EXPECT_FALSE(parse("<html>\n<body><p></body></html>"));
  EXPECT_TRUE(has("ERR:EHP:Parse error at line 2:\nmismatched tag"));
  ChapterLogs::lines.clear();
  EXPECT_TRUE(parse("<html><body>ok</body></html>trailing"));
  EXPECT_TRUE(has("DBG:EHP:Ignoring trailing data after </html>: junk after document element"));
}
TEST_F(ChapterDiagnostics, ImagesAndNestedTables) {
  parser.epub = std::make_shared<Epub>();
  parser.epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 8, 0, 4, 0};
  Epub::enableGifProbeForTest = true;
  EXPECT_TRUE(
      parse("<html><body><img src='probe.gif'/><img src='probe.gif' style='width:4px'/>"
            "<img src='probe.gif' style='height:2px'/><img src='probe.gif' style='width:4px;height:2px'/>"
            "<table><tr><td>outer<table><tr><td>inner</td></tr></table></td></tr></table></body></html>"));
  EXPECT_TRUE(has("DBG:EHP:Found image: src=probe.gif"));
  EXPECT_TRUE(has("DBG:EHP:Image dimensions: 8x4"));
  EXPECT_TRUE(has("DBG:EHP:Display size: 8x4 (scale 1.00)"));
  EXPECT_TRUE(has("DBG:EHP:Display size from CSS width: 4x2"));
  EXPECT_TRUE(has("DBG:EHP:Display size from CSS height: 4x2"));
  EXPECT_TRUE(has("DBG:EHP:Display size from CSS height+width: 4x2"));
  EXPECT_TRUE(has("DBG:EHP:nested table flattened into enclosing cell"));
}
TEST_F(ChapterDiagnostics, InitialParagraphFailure) {
  ASSERT_TRUE(parser.ensureSemantics());
  xp_test_fail_allocation_after(0);
  parser.beginAssembly();
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_TRUE(has("ERR:EHP:OOM: ParsedText"));
  parser.abortParse();
}
TEST_F(ChapterDiagnostics, AllocationSchedules) {
  const std::vector<std::string> bodies = {
      "<p>one two three four five</p>",
      "<hr/>",
      "<p><span style='float:left;font-size:3em'>T</span>ext one two three four five</p>",
      "<div style='border:1px solid;background:#eee'><p>border</p></div>",
      "<table><tr><td>left</td><td>right</td></tr></table>",
      "<table style='border:1px solid'><tr><td style='border:1px solid'>left</td><td>right</td></tr></table>",
      "<img src='probe.gif'/>",
      "<table><tr><td>single cell</td></tr></table>",
  };
  std::set<std::string> found;
  const char* output = std::getenv("CHAPTER_DIAGNOSTIC_TRACE");
  std::ofstream trace;
  if (output) trace.open(output);
  parser.epub = std::make_shared<Epub>();
  parser.epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 8, 0, 4, 0};
  Epub::enableGifProbeForTest = true;
  const size_t fixtureCount = std::getenv("CHAPTER_RESOURCE_BASELINE") ? 7 : bodies.size();
  for (size_t fixture = 0; fixture < fixtureCount; ++fixture) {
    const auto html = "<html><body>" + bodies[fixture] + "</body></html>";
    size_t required = 0;
    for (size_t run = 0; run == 0 || run <= required; ++run) {
      ChapterLogs::lines.clear();
      xp_test_fail_allocation_after(run == 0 ? SIZE_MAX : run - 1);
      const bool ok = parse(html);
      const auto attempts = xp_test_allocation_attempts();
      xp_test_fail_allocation_after(SIZE_MAX);
      if (run == 0) {
        required = attempts;
        EXPECT_TRUE(ok);
      }
      EXPECT_EQ(xp_test_live_allocations(), 0u);
      trace << fixture << ' ' << run << ' ' << ok << ' ' << attempts << '\n';
      found.insert(ChapterLogs::lines.begin(), ChapterLogs::lines.end());
    }
  }
  for (const auto& line : found) std::fprintf(stderr, "diagnostic: %s\n", line.c_str());
  for (const char* message :
       {"OOM: ParsedText", "OOM: PageBorderBox", "OOM: PageDropCap", "Failed to create PageHorizontalRule",
        "OOM: table cell", "Failed to create ImageBlock", "Failed to create PageImage", "OOM: PageLine",
        "OOM: table cell border", "OOM: text block after table", "OOM: table row separator",
        "Text layout dropped content (OOM); failing section build"}) {
    if (fixtureCount == 7 && std::string_view(message) == "OOM: table row separator") continue;
    EXPECT_NE(found.find(std::string("ERR:EHP:") + message), found.end()) << message;
  }
  EXPECT_NE(found.find("ERR:PTX:Dropping line: TextBlock or arena allocation failed"), found.end());
}
TEST_F(ChapterDiagnostics, WordArenaFailure) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  auto* paragraph = xp_chapter_test_paragraph(parser.semanticState());
  xp_test_fail_array_bytes(2048);
  const std::string word = "word";
  xp_para_add_word(paragraph, {reinterpret_cast<const uint8_t*>(word.data()), 4}, 0, 0, 0, 0, 0, 0, 0);
  xp_test_fail_array_bytes(0);
  EXPECT_TRUE(has("ERR:PTX:OOM: dropping paragraph text (arena chunk alloc failed)"));
  EXPECT_FALSE(has("ERR:PTX:OOM: paragraph storage (2048 bytes)"));
  parser.abortParse();
}
TEST_F(ChapterDiagnostics, NoParagraphAndSoftFlush) {
  ASSERT_TRUE(parser.ensureSemantics());
  xp_chapter_test_assembly(parser.semanticState(), 102, 0);
  EXPECT_TRUE(has("ERR:EHP:!! No text block to make pages for !!"));
  parser.abortParse();
  std::string body = "<html><body><p>";
  for (int i = 0; i < 1000; ++i) body += "a ";
  body += "</p></body></html>";
  EXPECT_TRUE(parse(body));
  EXPECT_TRUE(std::any_of(ChapterLogs::lines.begin(), ChapterLogs::lines.end(),
                          [](const auto& line) { return line.starts_with("DBG:EHP:Text block soft flush ("); }));
}
TEST_F(ChapterDiagnostics, Corpus) {
  const char* directory = std::getenv("CHAPTER_DIAGNOSTIC_CORPUS");
  const char* output = std::getenv("CHAPTER_DIAGNOSTIC_LOGS");
  if (!directory || !output) GTEST_SKIP();
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) paths.push_back(entry.path());
  std::sort(paths.begin(), paths.end());
  std::ofstream logs(output);
  parser.epub = std::make_shared<Epub>();
  parser.epub->imageBytes = {'G', 'I', 'F', '8', '9', 'a', 8, 0, 4, 0};
  Epub::enableGifProbeForTest = true;
  for (const auto& path : paths) {
    std::ifstream input(path, std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(input), {}};
    ChapterLogs::lines.clear();
    const bool ok = parse(html);
    logs << path.filename().string() << '\t' << ok << '\n';
    for (const auto& line : ChapterLogs::lines) {
      if (!(line.starts_with("DBG:EHP:") || line.starts_with("ERR:EHP:") || line.starts_with("ERR:PTX:") ||
            line.starts_with("DBG:WST:")) ||
          line.find("Time to parse and build pages") != std::string::npos)
        continue;
      logs << "log\t";
      const char* digits = "0123456789abcdef";
      for (unsigned char c : line) logs << digits[c >> 4] << digits[c & 15];
      logs << '\n';
    }
  }
}

TEST_F(ChapterDiagnostics, CharacterParagraphFailure) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  const XpChapterSpan body{reinterpret_cast<const uint8_t*>("body"), 4};
  xp_chapter_test_event(parser.semanticState(), 0, &body, 1);
  xp_chapter_test_assembly(parser.semanticState(), 101, 0);
  xp_test_fail_allocation_after(2);
  const XpChapterSpan word{reinterpret_cast<const uint8_t*>("word"), 4};
  xp_chapter_test_event(parser.semanticState(), 1, &word, 1);
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_TRUE(has("ERR:EHP:OOM: text block for character data"));
  parser.abortParse();
}
TEST_F(ChapterDiagnostics, NfcSourceOffsetsFailure) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  struct Chunk {
    void* next;
    uint32_t base;
    uint16_t deltas[64];
    uint8_t begin, count;
  };
  xp_test_fail_array_bytes(sizeof(Chunk));
  const std::string text = "\xe1\x84\x80\xe1\x85\xa1";
  xp_para_add_word(xp_chapter_test_paragraph(parser.semanticState()),
                   {reinterpret_cast<const uint8_t*>(text.data()), static_cast<uint32_t>(text.size())}, 0, 0, 0, 0, 0,
                   0, 0);
  xp_test_fail_array_bytes(0);
  EXPECT_TRUE(has("ERR:PTX:OOM: NFC source offsets (" + std::to_string(sizeof(Chunk)) + " bytes)"));
  parser.abortParse();
}
TEST_F(ChapterDiagnostics, PageLinkLimit) {
  std::string html = "<html><body><p>";
  for (int i = 0; i < 33; ++i) html += "<a href='#link" + std::to_string(i) + "'>x</a> ";
  html += "</p></body></html>";
  EXPECT_TRUE(parse(html));
  EXPECT_TRUE(has("DBG:EHP:Dropped page link: #link32"));
}

TEST_F(ChapterDiagnostics, CacheReclaimRescuesChunk) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  ChapterAllocation::recover = true;
  xp_test_fail_array_bytes(2048);
  const std::string text = "word";
  xp_para_add_word(xp_chapter_test_paragraph(parser.semanticState()),
                   {reinterpret_cast<const uint8_t*>(text.data()), 4}, 0, 0, 0, 0, 0, 0, 0);
  ChapterAllocation::recover = false;
  xp_test_fail_array_bytes(0);
  EXPECT_TRUE(has("DBG:WST:Cache eviction rescued a 2048-byte chunk allocation"));
  parser.abortParse();
}
TEST_F(ChapterDiagnostics, CacheReclaimForTokenChunkIsSilent) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  ChapterAllocation::recover = true;
  xp_test_fail_array_bytes(64 * sizeof(XpStoredWord));
  const std::string text = "word";
  xp_para_add_word(xp_chapter_test_paragraph(parser.semanticState()),
                   {reinterpret_cast<const uint8_t*>(text.data()), 4}, 0, 0, 0, 0, 0, 0, 0);
  ChapterAllocation::recover = false;
  xp_test_fail_array_bytes(0);
  EXPECT_FALSE(std::any_of(ChapterLogs::lines.begin(), ChapterLogs::lines.end(),
                           [](const auto& line) { return line.starts_with("DBG:WST:"); }));
  parser.abortParse();
}
bool chapterTestFailPageAllocation() { return false; }
void chapterTestBlockDestroyed(const void*) {}
TEST_F(ChapterDiagnostics, FinalLayoutFailure) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  const std::string text = "word";
  xp_para_add_word(xp_chapter_test_paragraph(parser.semanticState()),
                   {reinterpret_cast<const uint8_t*>(text.data()), 4}, 0, 0, 0, 0, 0, 0, 0);
  xp_test_fail_allocation_after(0);
  EXPECT_FALSE(parser.finishParse());
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_TRUE(has("ERR:EHP:Text layout dropped content (OOM); failing section build"));
}
TEST_F(ChapterDiagnostics, NullChapterCallback) {
  auto sink = parser.sink_;
  sink.diagnostic = nullptr;
  const auto options = parser.chapterOptions();
  ASSERT_EQ(xp_chapter_create(&parser.chapter_, &sink, &parser, cssParser.rustHandle(), &options), 0);
  xp_test_fail_allocation_after(0);
  parser.beginAssembly();
  xp_test_fail_allocation_after(SIZE_MAX);
  EXPECT_TRUE(parser.semanticOom());
  EXPECT_FALSE(has("ERR:EHP:OOM: ParsedText"));
  parser.abortParse();
}

TEST_F(ChapterDiagnostics, TokenChunkKeepsGenericStorageError) {
  ASSERT_TRUE(parser.ensureSemantics());
  parser.beginAssembly();
  xp_test_fail_array_bytes(64 * sizeof(XpStoredWord));
  const std::string text = "word";
  xp_para_add_word(xp_chapter_test_paragraph(parser.semanticState()),
                   {reinterpret_cast<const uint8_t*>(text.data()), 4}, 0, 0, 0, 0, 0, 0, 0);
  xp_test_fail_array_bytes(0);
  EXPECT_TRUE(has("ERR:PTX:OOM: paragraph storage (512 bytes)"));
  EXPECT_FALSE(std::any_of(ChapterLogs::lines.begin(), ChapterLogs::lines.end(),
                           [](const auto& line) { return line.starts_with("ERR:PTX:OOM: NFC source offsets"); }));
  parser.abortParse();
}
