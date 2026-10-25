#include <gtest/gtest.h>
#include <xp_ffi.h>

#include <array>
#include <string>
#include <vector>

extern "C" void cssTestFailAllocation(size_t index);
extern "C" size_t cssTestAllocationAttempts();

namespace {
std::string stylesheet() {
  std::string css;
  for (size_t i = 0; i < 1500; ++i) {
    css += ".class-" + std::to_string(i) + "{margin-top:" + std::to_string(i % 256) + "px}";
  }
  return css;
}
uint8_t feed(XpCss* handle, const std::string& css) {
  uint8_t result = 0;
  for (size_t pos = 0; pos < css.size(); pos += 512) {
    result |= xp_css_feed(handle, reinterpret_cast<const uint8_t*>(css.data() + pos),
                          std::min<size_t>(512, css.size() - pos), false);
  }
  return result | xp_css_feed(handle, nullptr, 0, true);
}
std::vector<uint8_t> cache(XpCss* handle) {
  std::vector<uint8_t> bytes;
  std::array<uint8_t, XP_CSS_CACHE_BUFFER_BYTES> record{};
  xp_css_cache_write_begin(handle, true);
  while (true) {
    size_t written = 0;
    EXPECT_EQ(xp_css_cache_write(handle, record.data(), record.size(), &written), 0);
    if (written == 0) break;
    bytes.insert(bytes.end(), record.begin(), record.begin() + written);
  }
  return bytes;
}
TEST(CssAllocationTest, EachAllocationFailureDuringParseRemainsPartial) {
  const auto css = stylesheet();
  cssTestFailAllocation(SIZE_MAX);
  auto* handle = xp_css_create();
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(feed(handle, css), 0);
  const size_t count = cssTestAllocationAttempts();
  xp_css_destroy(handle);
  ASSERT_GT(count, 15u);
  for (size_t failAt = 0; failAt < count; ++failAt) {
    SCOPED_TRACE(failAt);
    cssTestFailAllocation(failAt);
    handle = xp_css_create();
    if (failAt == 0) { EXPECT_EQ(handle, nullptr); continue; }
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(feed(handle, css), 1);
    EXPECT_LE(xp_css_rule_count(handle), 1500u);
    xp_css_clear(handle);
    EXPECT_EQ(xp_css_rule_count(handle), 0u);
    cssTestFailAllocation(SIZE_MAX);
    EXPECT_EQ(feed(handle, ".valid{font-weight:bold}"), 0);
    EXPECT_EQ(xp_css_rule_count(handle), 1u);
    xp_css_destroy(handle);
  }
  cssTestFailAllocation(SIZE_MAX);
}
TEST(CssAllocationTest, EachCacheAllocationFailureReturnsLowMemoryAndCanRetry) {
  cssTestFailAllocation(SIZE_MAX);
  auto* writer = xp_css_create();
  ASSERT_NE(writer, nullptr);
  ASSERT_EQ(feed(writer, stylesheet()), 0);
  const auto bytes = cache(writer);
  xp_css_destroy(writer);
  auto* reader = xp_css_create();
  ASSERT_NE(reader, nullptr);
  cssTestFailAllocation(SIZE_MAX);
  ASSERT_EQ(xp_css_cache_read(reader, bytes.data(), bytes.size(), true), 0);
  const auto count = cssTestAllocationAttempts();
  xp_css_clear(reader);
  ASSERT_GT(count, 10u);
  for (size_t failAt = 0; failAt < count; ++failAt) {
    SCOPED_TRACE(failAt);
    cssTestFailAllocation(failAt);
    EXPECT_EQ(xp_css_cache_read(reader, bytes.data(), bytes.size(), true), 1);
    EXPECT_EQ(xp_css_rule_count(reader), 0u);
    cssTestFailAllocation(SIZE_MAX);
    EXPECT_EQ(xp_css_cache_read(reader, bytes.data(), bytes.size(), true), 0);
    EXPECT_EQ(xp_css_rule_count(reader), 1500u);
    xp_css_clear(reader);
  }
  xp_css_destroy(reader);
  cssTestFailAllocation(SIZE_MAX);
}
TEST(CssAllocationTest, ResolveAndInlineDoNotAllocate) {
  cssTestFailAllocation(SIZE_MAX);
  auto* handle = xp_css_create();
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(feed(handle, ".a{margin:1px} p.a{font-weight:bold}"), 0);
  const uint8_t tag[] = {'p'};
  const uint8_t classes[] = {'a'};
  const std::string declarations = "font-size:1.000000059604644775390625em;border:2pt solid";
  uint8_t wire[XP_CSS_WIRE_BYTES];
  cssTestFailAllocation(0);
  for (size_t i = 0; i < 100; ++i) {
    xp_css_resolve(handle, {tag, sizeof(tag)}, {classes, sizeof(classes)}, {nullptr, 0}, nullptr, 0, false, wire);
    EXPECT_EQ(wire[2], 1);
    xp_css_inline(reinterpret_cast<const uint8_t*>(declarations.data()), declarations.size(), wire);
  }
  EXPECT_EQ(cssTestAllocationAttempts(), 0u);
  cssTestFailAllocation(SIZE_MAX);
  xp_css_destroy(handle);
}
TEST(CssAllocationTest, CacheTruncationAndTrailingBytesAreInvalid) {
  auto* writer = xp_css_create();
  ASSERT_NE(writer, nullptr);
  ASSERT_EQ(feed(writer, ".a{margin:1px}.b{font-style:italic}"), 0);
  auto bytes = cache(writer);
  xp_css_destroy(writer);
  auto* reader = xp_css_create();
  ASSERT_NE(reader, nullptr);
  for (size_t len = 0; len < bytes.size(); ++len) {
    EXPECT_EQ(xp_css_cache_read(reader, bytes.data(), len, true), 2) << len;
    EXPECT_EQ(xp_css_rule_count(reader), 0u);
  }
  bytes.push_back(0);
  EXPECT_EQ(xp_css_cache_read(reader, bytes.data(), bytes.size(), true), 2);
  xp_css_destroy(reader);
}
}  // namespace
