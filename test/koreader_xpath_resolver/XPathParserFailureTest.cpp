#include <ChapterXPathResolver.h>
#include <Logging.h>
#include <gtest/gtest.h>

extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();
extern "C" void xp_test_fail_allocation_after(size_t count);

TEST(KOReaderXPathResolver, RustAllocationFailuresReturnEmptyAndReleaseParser) {
  auto epub = std::make_shared<Epub>(std::vector<std::string>{
      "<html><body><section><p>before<!--boundary--><![CDATA[é中]]>after</p></section></body></html>"});
  const char* expected[] = {"/body/DocFragment[1]/body/section[1]/p[1]",
                            "/body/DocFragment[1]/body/section[1]/p[1]/text()[2].1",
                            "/body/DocFragment[1]/body/section[1]/p[1]/text()[3].5"};
  for (int method = 0; method != 3; ++method) {
    bool completed = false;
    for (size_t allocations = 0; allocations < 256; ++allocations) {
      xp_test_fail_allocation_after(allocations);
      std::string result;
      switch (method) {
        case 0:
          result = ChapterXPathResolver::findXPathForParagraph(epub, 0, 1);
          break;
        case 1:
          result = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7);
          break;
        case 2:
          result = ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f);
          break;
      }
      xp_test_fail_allocation_after(SIZE_MAX);
      ASSERT_EQ(xp_test_live_allocations(), 0u) << method << ": " << allocations;
      if (!result.empty()) {
        EXPECT_EQ(result, expected[method]);
        completed = true;
        break;
      }
    }
    EXPECT_TRUE(completed) << method;
  }
}

TEST(KOReaderXPathResolver, DiagnosticsKeepOriginalErrorTextAndFinalPrefix) {
  const std::pair<std::string, std::string> cases[] = {
      {"<html><body></wrong>", "XML parse error: mismatched tag"},
      {"", "Final XML parse error: no element found"},
      {"<html><body", "Final XML parse error: unclosed token"},
      {"<html><body a='1' a='2'>", "XML parse error: duplicate attribute"},
      {"<html><body>&missing;</body></html>", "XML parse error: undefined entity"},
  };
  for (const auto& [xml, error] : cases) {
    auto epub = std::make_shared<Epub>(std::vector<std::string>{xml});
    for (int method = 0; method < 3; ++method) {
      logging_stub::Capture logs;
      if (method == 0) ChapterXPathResolver::findXPathForParagraph(epub, 0, 99);
      if (method == 1) ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 999);
      if (method == 2) ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f);
      EXPECT_EQ(logs.lines, (std::vector<std::string>{"ERR\tKOX\t" + error}));
    }
  }
}
TEST(KOReaderXPathResolver, DiagnosticsDistinguishCreationAndParseOom) {
  auto epub = std::make_shared<Epub>(std::vector<std::string>{"<html><body><p>Hello</p></body></html>"});
  xp_test_fail_allocation_after(SIZE_MAX);
  ChapterXPathResolver::findXPathForParagraph(epub, 0, 99);
  const size_t allocations = xp_test_allocation_attempts();
  ASSERT_GT(allocations, 1u);
  for (size_t count = 0; count < allocations; ++count) {
    logging_stub::Capture logs;
    xp_test_fail_allocation_after(count);
    auto result = ChapterXPathResolver::findXPathForParagraph(epub, 0, 99);
    xp_test_fail_allocation_after(SIZE_MAX);
    ASSERT_FALSE(logs.lines.empty());
    EXPECT_EQ(logs.lines[0],
              count == 0 ? "ERR\tKOX\tFailed to create XML parser" : "ERR\tKOX\tXML parse error: out of memory");
    EXPECT_EQ(xp_test_live_allocations(), 0u);
  }
}
