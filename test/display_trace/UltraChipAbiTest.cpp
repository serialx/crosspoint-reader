#include <xp_display.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

static void check(bool value) {
  if (!value) std::abort();
}
struct Context {
  unsigned io = 0, allocated = 0, freed = 0;
  bool failPsram = false;
  uint8_t* lastAllocation = nullptr;
  uint32_t lastSize = 0;
  std::vector<uint32_t> caps;
};
static Context& context(void* p) { return *static_cast<Context*>(p); }

int main() {
  for (auto controller : {XP_DISPLAY_UC8179, XP_DISPLAY_UC8279_X3, XP_DISPLAY_UC8279_X4, XP_DISPLAY_UC8279C_A4}) {
    for (bool failPsram : {false, true}) {
      Context ctx;
      ctx.failPsram = failPsram;
      XpDisplayPlatform platform{XP_DISPLAY_PLATFORM_VERSION, sizeof(platform), &ctx,
                                 [](void* p, uint32_t bytes, uint32_t caps) -> void* {
                                   auto& c = context(p);
                                   c.caps.push_back(caps);
                                   if (c.failPsram && caps == XP_DISPLAY_ALLOC_PSRAM_ONLY) return nullptr;
                                   ++c.allocated;
                                   c.lastAllocation = static_cast<uint8_t*>(std::malloc(bytes));
                                   c.lastSize = bytes;
                                   check(c.lastAllocation != nullptr);
                                   std::fill_n(c.lastAllocation, bytes, 0xa5);
                                   return c.lastAllocation;
                                 },
                                 [](void* p, void* allocation) {
                                   check(allocation != nullptr);
                                   ++context(p).freed;
                                   std::free(allocation);
                                 }};
      XpDisplayBus bus{XP_DISPLAY_ABI_VERSION,
                       sizeof(bus),
                       &ctx,
                       [](void* p, uint8_t) { ++context(p).io; },
                       [](void* p, uint8_t) { ++context(p).io; },
                       [](void* p, const uint8_t*, uint16_t) { ++context(p).io; },
                       [](void* p, uint8_t, const uint8_t*, uint16_t) { ++context(p).io; },
                       [](void* p) { ++context(p).io; },
                       [](void* p, uint8_t) { ++context(p).io; },
                       [](void* p, uint8_t) { ++context(p).io; },
                       [](void* p, const uint8_t*, uint16_t) { ++context(p).io; },
                       [](void* p) { ++context(p).io; },
                       [](void* p, uint16_t) { ++context(p).io; },
                       [](void* p, uint8_t) { ++context(p).io; },
                       [](void*) -> uint8_t { return 0; },
                       [](void* p, uint32_t, uint32_t) { ++context(p).io; },
                       [](void* p, uint32_t) { ++context(p).io; },
                       [](void* p, uint32_t) { ++context(p).io; },
                       [](void* p, uint32_t) { ++context(p).io; },
                       [](void*) -> uint32_t { return 0; },
                       [](void*, uint32_t, uint32_t) -> uint32_t { return 0; },
                       nullptr};
      alignas(XP_DISPLAY_STATE_ALIGN) std::array<uint8_t, XP_DISPLAY_STATE_BYTES> state;
      state.fill(0xa5);
      const auto untouched = state;
      const uint16_t width = controller == XP_DISPLAY_UC8279_X3 ? 792 : controller == XP_DISPLAY_UC8279C_A4 ? 768 : 800;
      const uint16_t height = controller == XP_DISPLAY_UC8279_X3    ? 528
                              : controller == XP_DISPLAY_UC8279C_A4 ? 552
                                                                    : 480;
      XpDisplayUltraChipConfig registers{
          0x3f, 0x0a, 0x20, 0x0e, {0x25, 0x25, 0x3c, 0x25}, 2, 2, 0x1e, 0x5a, 0x29, 0xa9, 0xd7, 0x22, 600, 0,
          1,    0,    1,    0};
      const bool x4 = controller == XP_DISPLAY_UC8179 || controller == XP_DISPLAY_UC8279_X4;
      XpDisplayConfig config{XP_DISPLAY_ABI_VERSION,
                             sizeof(config),
                             static_cast<uint32_t>(controller),
                             controller == XP_DISPLAY_UC8279_X4 ? 0x67u : 0u,
                             width,
                             height,
                             0,
                             x4 ? &registers : nullptr,
                             x4 ? static_cast<uint32_t>(sizeof(registers)) : 0u};
      XpDisplayInitOptions options{XP_DISPLAY_INIT_OPTIONS_VERSION, sizeof(options), 1, &platform};
      check(xp_display_state_size(controller) <= state.size());
      check(xp_display_state_align(controller) <= XP_DISPLAY_STATE_ALIGN);
      check(xp_display_init_v2(state.data(), 0, &config, &options) == XP_DISPLAY_INVALID);
      check(state == untouched && ctx.caps.empty() && ctx.io == 0);
      config.width -= 1;
      check(xp_display_init_v2(state.data(), state.size(), &config, &options) == XP_DISPLAY_INVALID);
      check(state == untouched && ctx.caps.empty() && ctx.io == 0);
      config.width = width;
      check(xp_display_init_v2(state.data(), state.size(), &config, &options) == XP_DISPLAY_OK);
      if (controller == XP_DISPLAY_UC8279_X4)
        check(!(xp_display_capabilities(state.data(), 0) & XP_DISPLAY_CAP_OVERLAY));
      XpDisplayRequest request{};
      request.struct_size = sizeof(request);
      request.operation = XP_DISPLAY_OP_DISPLAY;
      uint8_t shortPlane = 0;
      request.data = &shortPlane;
      request.data_len = 1;
      uint32_t result = 99;
      check(xp_display_execute(state.data(), &bus, &request, &result) == XP_DISPLAY_INVALID);
      check(result == 0 && ctx.caps.empty() && ctx.io == 0);
      request.data = nullptr;
      request.operation = XP_DISPLAY_OP_BEGIN;
      check(xp_display_execute(state.data(), &bus, &request, &result) == XP_DISPLAY_OK);
      std::vector<uint8_t> plane(static_cast<size_t>(width) / 8 * height, 0x3c);
      request.operation = XP_DISPLAY_OP_GRAY_LSB;
      request.data = plane.data();
      request.data_len = plane.size();
      if (controller == XP_DISPLAY_UC8279C_A4) {
        request.operation = XP_DISPLAY_OP_GRAY_STRIP;
        request.height = 1;
        request.data_len = width / 8;
      }
      check(xp_display_execute(state.data(), &bus, &request, &result) == XP_DISPLAY_OK);
      if (controller == XP_DISPLAY_UC8279_X3) {
        check(ctx.caps.empty());
      } else {
        check(ctx.caps.front() == XP_DISPLAY_ALLOC_PSRAM_ONLY);
        const bool fallback = failPsram && controller != XP_DISPLAY_UC8179;
        check(ctx.caps.size() == (fallback ? 2u : 1u));
        if (fallback) check(ctx.caps.back() == XP_DISPLAY_ALLOC_DEFAULT);
      }
      if (controller == XP_DISPLAY_UC8279C_A4) {
        check(std::all_of(ctx.lastAllocation, ctx.lastAllocation + width / 8, [](uint8_t b) { return b == 0x3c; }));
        check(std::all_of(ctx.lastAllocation + width / 8, ctx.lastAllocation + ctx.lastSize,
                          [](uint8_t b) { return b == 0; }));
      }
      xp_display_drop(state.data());
      check(ctx.freed == ctx.allocated);
    }
  }
}
