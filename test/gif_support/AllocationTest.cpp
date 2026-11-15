#include <gtest/gtest.h>
#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();

namespace {
struct Source {
  std::vector<uint8_t> bytes;
  size_t readLimit = SIZE_MAX;
  static size_t read(void* context, uint32_t offset, uint8_t* out, size_t len) {
    auto& source = *static_cast<Source*>(context);
    if (offset >= source.bytes.size()) return 0;
    len = std::min({len, source.bytes.size() - offset, source.readLimit});
    std::memcpy(out, source.bytes.data() + offset, len);
    return len;
  }
  static void poll(void*) {}
  XpGifInput input() { return {this, read, poll}; }
  Source() {
    const char* hex =
        "47494638396108000400810000000000555555aaaaaaffffff21f90401000003002c000000000800040000081500010410304020c18105"
        "1102206830614286030302003b";
    for (size_t i = 0; i < std::strlen(hex); i += 2) {
      auto digit = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
      bytes.push_back(digit(hex[i]) * 16 + digit(hex[i + 1]));
    }
  }
};
struct Sink {
  std::array<uint8_t, 8> frame{};
  int opens = 0, aborts = 0, writes = 0;
  int failWrite = -1;
  bool failOpen = false, failClose = false;
  static void target(void* context, XpImageTarget* out) {
    auto& self = *static_cast<Sink*>(context);
    *out = {self.frame.data(), self.frame.size(), 1, 0, 8, 8, 8, 0, 3, 0};
  }
  static bool io(void* context, uint32_t op, const uint8_t*, size_t) {
    auto& self = *static_cast<Sink*>(context);
    switch (op) {
      case 0:
        ++self.opens;
        return !self.failOpen;
      case 1:
        return self.writes++ != self.failWrite;
      case 2:
        return !self.failClose;
      case 3:
        ++self.aborts;
        return true;
    }
    return false;
  }
  static bool write(void* context, const uint8_t* data, size_t len) { return io(context, 1, data, len); }
};

TEST(GifAllocations, ReleasesEveryOwnerWhenEachRequestFails) {
  Source source;
  auto input = source.input();
  for (const bool bmp : {false, true}) {
    size_t required = 0;
    for (size_t run = 0; run <= required + 1; ++run) {
      const size_t budget = run == 0 ? SIZE_MAX : run - 1;
      Sink sink;
      const XpImageConfig frame{0, 0, 4, 4, 8, 8, 0, 0, 1, 1, 0, 2};
      const XpImageCallbacks callbacks{&sink, Sink::target, Sink::io};
      const XpGifBmpConfig config{4, 4, 1, 0, 0};
      const XpGifOutput output{&sink, Sink::write};
      xp_test_fail_allocation_after(budget);
      const bool ok = bmp ? xp_gif_bmp(&input, &config, &output) : xp_gif_frame(&input, &frame, &callbacks, true);
      if (run == 0) required = xp_test_allocation_attempts();
      EXPECT_EQ(ok, budget >= required) << "bmp=" << bmp << " budget=" << budget;
      EXPECT_EQ(xp_test_live_allocations(), 0u);
      if (!bmp && sink.opens && !ok) EXPECT_EQ(sink.aborts, 1);
      xp_test_fail_allocation_after(SIZE_MAX);
    }
  }
}

TEST(GifAllocations, PropagatesInputAndOutputFailures) {
  for (int failure = -3; failure < 6; ++failure) {
    Source source;
    const auto input = source.input();
    Sink sink;
    sink.failOpen = failure == -3;
    sink.failClose = failure == -2;
    sink.failWrite = failure;
    const XpImageConfig frame{0, 0, 4, 4, 8, 8, 0, 0, 1, 1, 0, 2};
    const XpImageCallbacks callbacks{&sink, Sink::target, Sink::io};
    const bool ok = xp_gif_frame(&input, &frame, &callbacks, true);
    EXPECT_EQ(ok, failure == -1);
    EXPECT_EQ(xp_test_live_allocations(), 0u);
    if (!ok && failure != -3) EXPECT_EQ(sink.aborts, 1);
  }
  Source source;
  source.readLimit = 1;
  const auto input = source.input();
  XpGifInfo info;
  EXPECT_FALSE(xp_gif_inspect(&input, &info));
  EXPECT_EQ(xp_test_live_allocations(), 0u);
}
}  // namespace
