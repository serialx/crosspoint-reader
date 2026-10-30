#include <xp_display.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
void check(bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "%s\n", message);
    std::abort();
  }
}
struct Trace {
  unsigned attempts = 0, failAt = 0, live = 0, frees = 0, diagnostic = 0, requested = 0;
  unsigned clockReads = 0, refreshEvents = 0;
  uint32_t clock = 0xffffffe0;
  std::array<uint32_t, 4> refresh{};
  std::vector<unsigned> commands;
  std::vector<unsigned> allocations;
  std::vector<unsigned> liveAtRefresh;
};
void* allocate(void* ctx, uint32_t size, uint32_t caps) {
  auto& t = *static_cast<Trace*>(ctx);
  check(caps == XP_DISPLAY_ALLOC_DEFAULT, "window must use default malloc policy");
  t.allocations.push_back(size);
  if (++t.attempts == t.failAt) return nullptr;
  ++t.live;
  return std::malloc(size);
}
void release(void* ctx, void* p) {
  auto& t = *static_cast<Trace*>(ctx);
  check(t.live != 0, "double free");
  --t.live;
  ++t.frees;
  std::free(p);
}
XpDisplayBus bus(Trace& trace) {
  return {XP_DISPLAY_ABI_VERSION,
          sizeof(XpDisplayBus),
          &trace,
          [](void* ctx, uint8_t command) {
            auto& t = *static_cast<Trace*>(ctx);
            t.commands.push_back(command);
            if (command == 0x20) t.liveAtRefresh.push_back(t.live);
          },
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
          [](void*, uint32_t) {},
          [](void*, uint32_t) {},
          [](void*, uint32_t) {},
          [](void* ctx) -> uint32_t {
            auto& t = *static_cast<Trace*>(ctx);
            ++t.clockReads;
            t.clock += 17;
            return t.clock;
          },
          [](void*, uint32_t, uint32_t) -> uint32_t { return 0; },
          [](void* ctx, uint32_t event, uint32_t bytes, uint32_t b, uint32_t c, uint32_t d, const uint8_t*, uint32_t) {
            auto& t = *static_cast<Trace*>(ctx);
            if (event == XP_DISPLAY_DIAG_SSD_REFRESH) {
              ++t.refreshEvents;
              t.refresh = {bytes, b, c, d};
            }
            t.diagnostic = event;
            t.requested = bytes;
          }};
}
}  // namespace
int main() {
  alignas(XP_DISPLAY_STATE_ALIGN) std::array<uint8_t, XP_DISPLAY_STATE_BYTES> storage{};
  XpDisplayConfig config{XP_DISPLAY_ABI_VERSION, sizeof(config), XP_DISPLAY_SSD1677, 0, 800, 480, 0, nullptr, 0};
  check(xp_display_state_size(XP_DISPLAY_SSD1677) <= storage.size(), "state bound");
  Trace t;
  auto io = bus(t);
  XpDisplayPlatform platform{XP_DISPLAY_PLATFORM_VERSION, sizeof(platform), &t, allocate, release};
  XpDisplayInitOptions options{XP_DISPLAY_INIT_OPTIONS_VERSION, sizeof(options), 1, &platform};
  std::vector<uint8_t> frame(48000, 0xa5), previous(48000, 0x5a);
  auto init = [&] {
    check(xp_display_init_v2(storage.data(), storage.size(), &config, &options) == XP_DISPLAY_OK, "init");
  };
  auto run = [&](XpDisplayRequest r, uint32_t expected) {
    uint32_t result = 0xffffffff;
    check(xp_display_execute(storage.data(), &io, &r, &result) == expected, "execute status");
    if (expected != XP_DISPLAY_OK) check(result == 0, "failure must not commit");
    return result;
  };
  XpDisplayRequest r{};
  r.struct_size = sizeof(r);
  r.operation = XP_DISPLAY_OP_DISPLAY_WINDOW;
  r.x = 16;
  r.y = 17;
  r.width = 88;
  r.height = 33;
  r.data = frame.data();
  r.data_len = frame.size();
  r.previous = previous.data();
  r.previous_len = previous.size();
  for (unsigned failAt : {1u, 2u, 0u}) {
    t = {};
    t.failAt = failAt;
    init();
    run(r, failAt ? XP_DISPLAY_INVALID : XP_DISPLAY_OK);
    check(t.live == 0, "window storage leaked");
    check(t.allocations == std::vector<unsigned>(failAt == 1 ? 1 : 2, 363), "window allocation sequence");
    check(t.frees == (failAt == 1 ? 0 : failAt == 2 ? 1 : 2), "window free sequence");
    if (failAt) {
      check(t.diagnostic == XP_DISPLAY_DIAG_SSD_WINDOW_ALLOCATION && t.requested == 363, "OOM diagnostic");
      check(t.liveAtRefresh.empty(), "OOM triggered waveform");
      // A failed window may write BW, but a successful retry supplies both planes.
      t.failAt = 0;
      t.liveAtRefresh.clear();
      run(r, XP_DISPLAY_OK);
    }
    check(t.liveAtRefresh == std::vector<unsigned>{1}, "previous window retained across refresh");
    xp_display_drop(storage.data());
  }
  t = {};
  init();
  r.previous = nullptr;
  run(r, XP_DISPLAY_OK);
  check(t.allocations == std::vector<unsigned>{363} && t.frees == 1, "single window allocation");
  r.x = 13;
  t.commands.clear();
  run(r, XP_DISPLAY_OK);
  check(t.commands.empty(), "unaligned window issued commands");
  r.x = 16;
  r.data_len = 1;
  run(r, XP_DISPLAY_INVALID);
  check(t.commands.empty(), "short input issued commands");
  r.operation = XP_DISPLAY_OP_GRAY_STRIP;
  r.data_len = 48000;
  r.y = 479;
  r.height = 2;
  run(r, XP_DISPLAY_INVALID);
  check(t.commands.empty(), "out-of-range strip issued commands");
  r.y = 0;
  r.height = 1;
  r.data_len = 99;
  run(r, XP_DISPLAY_INVALID);
  check(t.commands.empty(), "short strip issued commands");
  r.operation = XP_DISPLAY_OP_GRAY_LSB;
  r.data = nullptr;
  run(r, XP_DISPLAY_OK);
  check(t.commands.empty(), "missing gray plane issued commands");
  r.operation = XP_DISPLAY_OP_GRAY_BEGIN;
  r.mode = 1;
  r.arg = 2;
  run(r, XP_DISPLAY_OK);
  check(t.commands.empty(), "absolute begin needs no base frame");
  r.operation = XP_DISPLAY_OP_CUSTOM_LUT;
  r.arg = 1;
  run(r, XP_DISPLAY_INVALID);
  check(t.commands.empty(), "missing LUT issued commands");
  r.operation = XP_DISPLAY_OP_QUERY;
  r.arg = 0;
  check(run(r, XP_DISPLAY_OK) == XP_DISPLAY_RESULT_COMMITTED, "SSD1677 commit contract");
  xp_display_drop(storage.data());
  // Reject initialization without touching caller storage.
  storage.fill(0x5a);
  const auto before = storage;
  config.height = 656;
  check(xp_display_init_v2(storage.data(), storage.size(), &config, &options) == XP_DISPLAY_INVALID, "oversized frame");
  check(storage == before, "invalid init modified storage");
  config.height = 480;
  XpDisplaySsd1677Config custom{};
  check(xp_display_ssd1677_default_config(nullptr) == XP_DISPLAY_INVALID, "null default output");
  check(xp_display_ssd1677_default_config(&custom) == XP_DISPLAY_OK, "default config export");
  const auto defaults = custom;
  config.controller_config = &custom;
  config.controller_config_size = sizeof(custom);
  const auto reject = [&] {
    storage.fill(0x5a);
    const auto original = storage;
    t = {};
    check(xp_display_init_v2(storage.data(), storage.size(), &config, &options) == XP_DISPLAY_INVALID,
          "invalid descriptor accepted");
    check(storage == original && t.allocations.empty() && t.commands.empty(), "invalid init had effects");
  };
  for (uint32_t size = 0; size < sizeof(custom); ++size) {
    config.controller_config_size = size;
    reject();
  }
  config.controller_config_size = sizeof(custom);
  for (uint32_t version : {0u, 2u, 0xffffffffu}) {
    custom.version = version;
    reject();
  }
  custom = defaults;
  custom.struct_size--;
  reject();
  for (uint8_t* field : {&custom.gray_power_first, &custom.absolute, &custom.overlay, &custom.second_control,
                         &custom.restore_temp, &custom.black_pulse}) {
    custom = defaults;
    *field = 2;
    reject();
  }
  for (auto* lut : {&custom.gray, &custom.factory, &custom.fast_lut, &custom.full_lut, &custom.half_lut}) {
    for (uint32_t size : {0u, 1u, 109u, 111u, 0xffffffffu}) {
      custom = defaults;
      *lut = {defaults.gray.data, size};
      reject();
    }
    custom = defaults;
    *lut = {nullptr, 110};
    reject();
  }
  custom = defaults;
  custom.gray = {nullptr, 0};
  reject();
  custom = defaults;
  config.controller_config = reinterpret_cast<const char*>(&custom) + 1;
  reject();
  config.controller_config = &custom;
  options.controller_config_version = 2;
  reject();
  options.controller_config_version = 1;
  for (bool diagnostics : {false, true}) {
    t = {};
    config.flags = diagnostics ? XP_DISPLAY_SSD1677_DIAGNOSTICS : 0;
    init();
    r = {};
    r.struct_size = sizeof(r);
    r.operation = XP_DISPLAY_OP_DISPLAY;
    r.mode = 0;
    r.data = frame.data();
    r.data_len = frame.size();
    run(r, XP_DISPLAY_OK);
    check(t.allocations.empty(), "custom descriptor allocated");
    check(t.clockReads == (diagnostics ? 2u : 0u), "diagnostic clock gating");
    check(t.refreshEvents == (diagnostics ? 1u : 0u), "refresh event count");
    if (diagnostics) check(t.refresh == std::array<uint32_t, 4>{0, 17, 0xf7, 1}, "refresh payload/wrapping time");
    xp_display_drop(storage.data());
  }
  std::printf("SSD1677 ABI: allocation failures, cleanup, retries, bounds and null inputs passed; state=%u bytes\n",
              xp_display_state_size(XP_DISPLAY_SSD1677));
}
