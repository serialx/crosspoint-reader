#include <ChapterXPathResolver.h>
#include <gtest/gtest.h>

extern "C" size_t xp_test_live_allocations();
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
