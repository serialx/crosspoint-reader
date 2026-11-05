#include <xp_display.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;

  void suspend() {
    std::unique_lock lock(mutex);
    entered = true;
    changed.notify_one();
    changed.wait(lock, [&] { return released; });
  }
  void wait() {
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return entered; });
  }
  void resume() {
    std::lock_guard lock(mutex);
    released = true;
    changed.notify_one();
  }
};

struct FakeBus {
  Gate gate;
  bool pauseAllocation = false, pauseRefresh = false;
  unsigned refreshes = 0, allocated = 0, freed = 0;
};

static void overlap(bool duringAllocation, bool abortWork) {
  alignas(XP_DISPLAY_STATE_ALIGN) uint8_t state[XP_DISPLAY_PAPER_MONO_STATE_BYTES];
  uint8_t scratch[16384];
  FakeBus fake;
  XpDisplayPlatform platform{1, sizeof(platform), &fake,
                             [](void* p, uint32_t size, uint32_t caps) -> void* {
                               auto& f = *static_cast<FakeBus*>(p);
                               assert(size == 48000 && caps == XP_DISPLAY_ALLOC_PSRAM_ONLY);
                               if (f.pauseAllocation) {
                                 f.pauseAllocation = false;
                                 f.gate.suspend();
                               }
                               ++f.allocated;
                               auto* block = std::calloc(1, size);
                               assert(block);
                               return block;
                             },
                             [](void* p, void* block) {
                               ++static_cast<FakeBus*>(p)->freed;
                               std::free(block);
                             }};
  XpDisplayInitOptions options{1, sizeof(options), 1, &platform};
  XpDisplayRetainedConfig retained{scratch, sizeof(scratch), 0};
  XpDisplayConfig config{1, sizeof(config), XP_DISPLAY_PAPER_MONO, 1, 800, 480, 0, &retained, sizeof(retained)};
  XpDisplayBus bus{XP_DISPLAY_BUS_VERSION,
                   sizeof(bus),
                   &fake,
                   [](void*, uint8_t) {},
                   [](void*, uint8_t) {},
                   [](void*, const uint8_t*, uint16_t) {},
                   [](void*, uint8_t, const uint8_t*, uint16_t) {},
                   [](void*) {},
                   [](void*, uint8_t) {},
                   [](void*, uint8_t) {},
                   [](void*, const uint8_t*, uint16_t) {},
                   [](void*) {},
                   [](void*, uint16_t) {},
                   [](void*, uint8_t) {},
                   [](void*) -> uint8_t { return 0; },
                   [](void*, uint32_t, uint32_t) {},
                   [](void* p, uint32_t) {
                     auto& f = *static_cast<FakeBus*>(p);
                     ++f.refreshes;
                     if (f.pauseRefresh) {
                       f.pauseRefresh = false;
                       f.gate.suspend();
                     }
                   },
                   [](void*, uint32_t) {},
                   [](void*, uint32_t) {},
                   [](void*) -> uint32_t { return 0; },
                   [](void*, uint32_t, uint32_t) -> uint32_t { return 0; },
                   nullptr,
                   nullptr};
  assert(xp_display_init_v2(state, sizeof(state), &config, &options) == XP_DISPLAY_OK);
  auto execute = [&](uint32_t operation, const std::vector<uint8_t>* data = nullptr) {
    XpDisplayRequest request{};
    request.struct_size = sizeof(request);
    request.operation = operation;
    request.data = data ? data->data() : nullptr;
    request.data_len = data ? data->size() : 0;
    uint32_t result = 0;
    assert(xp_display_execute(state, &bus, &request, &result) == XP_DISPLAY_OK);
    return result;
  };
  std::vector<uint8_t> white(48000, 255), gray(48000, 0x55);
  execute(XP_DISPLAY_OP_BEGIN_WORK);
  if (!duringAllocation) {
    execute(XP_DISPLAY_OP_SEED_PREVIOUS, &white);
    execute(XP_DISPLAY_OP_GRAY_LSB, &gray);
    execute(XP_DISPLAY_OP_GRAY_MSB, &gray);
  }
  fake.pauseAllocation = duringAllocation;
  fake.pauseRefresh = !duringAllocation;
  uint32_t result = 0;
  std::thread render([&] { result = execute(XP_DISPLAY_OP_GRAY_DISPLAY); });
  fake.gate.wait();
  if (abortWork) xp_display_abort(state);
  assert(xp_display_generation(state) == (abortWork ? 1u : 0u));
  fake.gate.resume();
  render.join();
  // Allocation overlap has no frame yet; a started waveform finishes synchronously.
  assert(result ==
         ((abortWork ? XP_DISPLAY_RESULT_ABORTED : 0u) | (duringAllocation ? 0u : XP_DISPLAY_RESULT_COMMITTED)));
  unsigned expectedRefreshes = duringAllocation ? 0u : 1u;
  assert(fake.refreshes == expectedRefreshes);
  if (duringAllocation) {
    // Stage a complete frame without restarting the canceled work generation.
    execute(XP_DISPLAY_OP_SEED_PREVIOUS, &white);
    execute(XP_DISPLAY_OP_GRAY_LSB, &gray);
    execute(XP_DISPLAY_OP_GRAY_MSB, &gray);
    assert(execute(XP_DISPLAY_OP_GRAY_DISPLAY) ==
           (abortWork ? XP_DISPLAY_RESULT_ABORTED : XP_DISPLAY_RESULT_COMMITTED));
    if (!abortWork) ++expectedRefreshes;
    assert(fake.refreshes == expectedRefreshes);
  }
  execute(XP_DISPLAY_OP_BEGIN_WORK);
  execute(XP_DISPLAY_OP_SEED_PREVIOUS, &white);
  execute(XP_DISPLAY_OP_GRAY_LSB, &white);
  execute(XP_DISPLAY_OP_GRAY_MSB, &white);
  assert(execute(XP_DISPLAY_OP_GRAY_DISPLAY) == XP_DISPLAY_RESULT_COMMITTED);
  assert(fake.refreshes == expectedRefreshes + 1);
  xp_display_drop(state);
  assert(fake.allocated == 8 && fake.freed == fake.allocated);
}

static unsigned allocations = 0;
static void* allocate(void*, uint32_t size, uint32_t caps) {
  assert(caps == XP_DISPLAY_ALLOC_PSRAM_ONLY);
  assert(size == 48000);
  ++allocations;
  return std::calloc(1, size);
}
int main() {
  overlap(true, false);  // No-abort control for the same-generation commit.
  overlap(true, true);
  overlap(false, true);
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
