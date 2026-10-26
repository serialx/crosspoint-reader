#include <xp_ffi.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();

static void require(bool result) {
  if (!result) std::abort();
}
struct Sink {
  std::array<uint8_t, 8> frame{};
  std::vector<uint8_t> bytes;
  int failWrite = -1;
  int writes = 0, opens = 0, closes = 0, aborts = 0;
  bool failOpen = false, failClose = false;
  static void target(void* context, XpImageTarget* out) {
    auto& s = *static_cast<Sink*>(context);
    *out = {s.frame.data(), s.frame.size(), 1, 0, 8, 8, 8, 0, 3, 0};
  }
  static bool io(void* context, uint32_t op, const uint8_t* bytes, size_t length) {
    auto& s = *static_cast<Sink*>(context);
    switch (op) {
      case 0:
        ++s.opens;
        return !s.failOpen;
      case 1:
        if (s.writes++ == s.failWrite) return false;
        s.bytes.insert(s.bytes.end(), bytes, bytes + length);
        return true;
      case 2:
        ++s.closes;
        return !s.failClose;
      case 3:
        ++s.aborts;
        s.bytes.clear();
        return true;
      default:
        return false;
    }
  }
};

int main() {
  for (uint8_t kind : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
    for (size_t fail = 0; fail < 5; ++fail) {
      Sink sink;
      XpImageStorage storage;
      XpImagePlan plan;
      XpImageConfig config{0, 0, 4, 4, 8, 8, 0, 0, 1, 0, 0, kind};
      const XpImageCallbacks callbacks{&sink, Sink::target, Sink::io};
      require(xp_image_init(&storage, &config, 4, 4, false, &callbacks, &plan));
      xp_test_fail_allocation_after(fail);
      if (xp_image_prepare(&storage)) xp_image_cache_begin(&storage);
      xp_image_destroy(&storage);
      require(xp_test_live_allocations() == 0);
      xp_test_fail_allocation_after(SIZE_MAX);
    }
  }
  for (int failure = -3; failure < 6; ++failure) {
    Sink sink;
    sink.failOpen = failure == -3;
    sink.failClose = failure == -2;
    sink.failWrite = failure;
    XpImageStorage storage;
    XpImagePlan plan;
    const XpImageConfig config{0, 0, 4, 4, 8, 8, 0, 0, 1, 0, 0, 0};
    const XpImageCallbacks callbacks{&sink, Sink::target, Sink::io};
    require(xp_image_init(&storage, &config, 4, 4, false, &callbacks, &plan));
    require(xp_image_prepare(&storage));
    const bool caching = xp_image_cache_begin(&storage);
    const std::array<uint8_t, 4> gray{0, 64, 128, 255};
    XpImageRow row{0, 0, 4, 1, 4, gray.data(), gray.size(), nullptr, 0, 0, 0, 8, 0};
    for (int y = 0; y < 4; ++y) {
      row.y = y;
      require(xp_image_row(&storage, &row));
    }
    bool success = false;
    const bool cached = xp_image_finish(&storage, &success);
    require(success);
    require(cached == (failure == -1));
    if (cached) require(sink.bytes == std::vector<uint8_t>({4, 0, 4, 0, 27, 27, 27, 27}));
    xp_image_destroy(&storage);
    require(xp_test_live_allocations() == 0);
    if (!cached) require(sink.aborts == (failure == -3 ? 0 : 1));
    if (failure == -3 || failure == 0 || failure == 1) require(!caching);
  }
  std::puts("Image allocation and cache failure checks passed");
}
