#include <xp_display.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <thread>

static void check(bool ok) {
  if (!ok) std::abort();
}
int main() {
  const uint32_t capacities[] = {0,
                                 XP_DISPLAY_UC8253_X3_STATE_BYTES,
                                 XP_DISPLAY_SSD1677_STATE_BYTES,
                                 XP_DISPLAY_UC8179_STATE_BYTES,
                                 XP_DISPLAY_UC8279_X3_STATE_BYTES,
                                 XP_DISPLAY_UC8279_X4_STATE_BYTES,
                                 XP_DISPLAY_UC8279C_A4_STATE_BYTES,
                                 XP_DISPLAY_SSD2677_STATE_BYTES,
                                 XP_DISPLAY_PAPER_MONO_STATE_BYTES};
  for (uint32_t controller = 1; controller <= 8; ++controller) {
    check(xp_display_state_size(controller) > 0);
    check(xp_display_state_size(controller) <= capacities[controller]);
    check(xp_display_state_align(controller) <= alignof(void*));
  }
  alignas(XP_DISPLAY_STATE_ALIGN) std::array<uint8_t, XP_DISPLAY_STATE_BYTES> storage;
  storage.fill(0x5a);
  auto original = storage;
  XpDisplayConfig config{
      XP_DISPLAY_ABI_VERSION, sizeof(XpDisplayConfig), XP_DISPLAY_UC8253_X3, 0, 792, 528, 0, nullptr, 0};
  check(xp_display_state_size(1) <= storage.size());
  check(xp_display_state_align(1) <= XP_DISPLAY_STATE_ALIGN);
  check(xp_display_state_size(0) == 0);
  check(xp_display_init(storage.data(), 0, &config) == XP_DISPLAY_INVALID);
  check(storage == original);
  config.width = 791;
  check(xp_display_init(storage.data(), storage.size(), &config) == XP_DISPLAY_INVALID);
  check(storage == original);
  config.width = 792;
  XpDisplayInitOptions options{XP_DISPLAY_INIT_OPTIONS_VERSION, sizeof(options), 1, nullptr};
  options.abi_version = 0;
  check(xp_display_init_v2(storage.data(), storage.size(), &config, &options) == XP_DISPLAY_INVALID);
  check(storage == original);
  options.abi_version = XP_DISPLAY_INIT_OPTIONS_VERSION;
  check(xp_display_init_v2(storage.data(), storage.size(), &config, &options) == XP_DISPLAY_OK);
  check(xp_display_generation(storage.data()) == 0);
  std::array<std::thread, 4> threads;
  for (auto& t : threads)
    t = std::thread([&] {
      for (unsigned i = 0; i < 10000; ++i) xp_display_abort(storage.data());
    });
  for (auto& t : threads) t.join();
  check(xp_display_generation(storage.data()) == 40000);
  check(xp_display_capabilities(storage.data(), 2) & XP_DISPLAY_CAP_COMBINED);
  check(xp_display_extra(storage.data(), 1, nullptr, 0, 1) == XP_DISPLAY_UNSUPPORTED);
  uint32_t result = 99;
  check(xp_display_execute(storage.data(), nullptr, nullptr, &result) == XP_DISPLAY_INVALID);
  check(result == 0);
  xp_display_drop(storage.data());
  config.controller = 99;
  check(xp_display_state_size(config.controller) == 0);
  check(xp_display_init(storage.data(), storage.size(), &config) == XP_DISPLAY_UNSUPPORTED);
  XpDisplayX3Config custom{};
  const uint8_t* direct[5]{};
  xp_display_x3_default_config(&custom, direct);
  check(custom.lut_len == 42 && custom.direct == direct && direct[0] != nullptr);
  custom.direct = nullptr;
  config.controller = XP_DISPLAY_UC8253_X3;
  config.controller_config = &custom;
  config.controller_config_size = sizeof(custom);
  check(xp_display_init(storage.data(), storage.size(), &config) == XP_DISPLAY_OK);
  check(!(xp_display_capabilities(storage.data(), 2) & XP_DISPLAY_CAP_ABSOLUTE));
  xp_display_drop(storage.data());
}
