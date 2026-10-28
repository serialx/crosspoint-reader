#include <xp_display.h>

#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <thread>

static unsigned allocations = 0;
static void* allocate(void*, uint32_t size, uint32_t caps) {
  assert(caps == XP_DISPLAY_ALLOC_PSRAM_ONLY);
  assert(size == 48000);
  ++allocations;
  return std::calloc(1, size);
}
int main() {
  alignas(XP_DISPLAY_STATE_ALIGN) static uint8_t state[XP_DISPLAY_PAPER_MONO_STATE_BYTES];
  static uint8_t scratch[16384];
  XpDisplayPlatform platform{1, sizeof(platform), nullptr, allocate, [](void*, void* p) { std::free(p); }};
  XpDisplayInitOptions options{1, sizeof(options), 1, &platform};
  XpDisplayRetainedConfig retained{scratch, sizeof(scratch), 0};
  XpDisplayConfig config{1, sizeof(config), XP_DISPLAY_PAPER_MONO, 1, 800, 480, 0, &retained, sizeof(retained)};
  std::memset(state, 0xA5, sizeof(state));
  assert(xp_display_init_v2(state, 1, &config, &options) == XP_DISPLAY_INVALID);
  for (auto value : state) assert(value == 0xA5);
  assert(allocations == 0);
  assert(xp_display_init_v2(state, sizeof(state), &config, &options) == XP_DISPLAY_OK);
  assert(allocations == 0);
  assert(xp_display_capabilities(state, 0) ==
         (XP_DISPLAY_CAP_OVERLAY | XP_DISPLAY_CAP_COMBINED | XP_DISPLAY_CAP_STRIPS | XP_DISPLAY_CAP_BUSY_STAGING));
  assert(xp_display_capabilities(state, 1) == 0);
  std::thread first([&] {
    for (int i = 0; i < 10000; ++i) xp_display_abort(state);
  });
  std::thread second([&] {
    for (int i = 0; i < 10000; ++i) xp_display_abort(state);
  });
  first.join();
  second.join();
  assert(xp_display_generation(state) == 20000);
  assert(xp_display_extra(state, 1, nullptr, 1, 1) == XP_DISPLAY_INVALID);
  const uint8_t value = 255;
  assert(xp_display_extra(state, 1, &value, 1, 1) == XP_DISPLAY_OK);
  assert(xp_display_extra(state, 2, nullptr, 0, 1) == XP_DISPLAY_OK);
  xp_display_drop(state);
  config.controller = XP_DISPLAY_SSD2677;
  retained.scratch_size = 1024;
  assert(xp_display_init_v2(state, XP_DISPLAY_SSD2677_STATE_BYTES, &config, &options) == XP_DISPLAY_OK);
  assert(xp_display_capabilities(state, 1) == (XP_DISPLAY_CAP_ABSOLUTE | XP_DISPLAY_CAP_COMBINED));
  assert(xp_display_capabilities(state, 2) == 0);
  xp_display_drop(state);
}
