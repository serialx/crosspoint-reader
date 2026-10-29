#include <FreeInkUI.h>
#include <xp_ui_keyboard.h>

#include <cassert>
#include <cstring>
#include <string>

namespace ui = freeink::ui;
static XpUiKeyboardLayouts layouts{
    nullptr,
    [](void*, uint8_t id, uint8_t sh, uint8_t sy, uint8_t la, uint8_t r) -> XpUiKeyboardRow {
      const auto& l = ui::builtinKeyboardLayout(ui::KeyboardLayoutId(id), sh, sy, true, la);
      if (r >= l.rowCount) return {};
      const auto& row = l.rows[r];
      return {row.count, row.insetUnits, uint8_t(row.independentKeyWidth), 0};
    },
    [](void*, uint8_t id, uint8_t sh, uint8_t sy, uint8_t la, uint8_t r, uint8_t c) -> XpUiKeyboardKey {
      const auto& l = ui::builtinKeyboardLayout(ui::KeyboardLayoutId(id), sh, sy, true, la);
      if (r >= l.rowCount || c >= l.rows[r].count) return {};
      const auto& key = l.rows[r].keys[c];
      return {key.label, key.output, key.alt, key.value, uint8_t(key.kind), key.widthUnits};
    }};
int main() {
  const auto& layer = ui::builtinKeyboardLayout(ui::KeyboardLayoutId::QwertyEn, false, false, true, false);
  const int16_t values[]{layer.rows[0].keys[0].value, layer.rows[0].keys[1].value, 8};
  for (uint32_t seed = 1; seed <= 128; ++seed) {
    ui::InteractionBuffer<3> hits;
    for (unsigned i = 0; i < 3; ++i)
      hits.addInteraction({{int16_t(100 + 40 * i), 100, 30, 30},
                           1,
                           values[i],
                           static_cast<uint16_t>(ui::InputTouch | ui::InputLongPress)});
    ui::TouchHoldRouter router;
    router.holdMs = 350;
    router.overrideHoldMs = 900;
    XpUiKeyboardState state{};
    xp_ui_keyboard_init(&state, 0, 0, 0, 1, 0);
    uint8_t bytes[1024]{};
    std::string expected;
    uint32_t random = seed, now = UINT32_MAX - 500;
    for (unsigned step = 0; step < 128; ++step) {
      random = random * 1664525 + 1013904223;
      now += (random >> 16) % 501;
      const unsigned key = (random >> 8) % 4, kind = random % 6;
      const bool press = kind < 3, tap = kind == 3, contact = press || kind == 4;
      const int16_t x = key < 3 ? 110 + key * 40 : 250;
      const auto result = router.update(hits, press, x, 110, tap, x, 110, contact, now);
      XpUiInput input{};
      input.now_ms = now;
      input.touch_x = x;
      input.touch_y = 110;
      input.touch_flags =
          (press ? XP_UI_TOUCH_PRESSED : 0) | (tap ? XP_UI_TOUCH_RELEASED : 0) | (contact ? XP_UI_TOUCH_HELD : 0);
      if (kind == 5) {
        input.touch_flags = XP_UI_TOUCH_RELEASED;
        input.touch_x = input.touch_y = -1;
      }
      if (key < 3 && kind != 5) {
        input.action = 1;
        input.value = values[key];
      }
      const auto effect = xp_ui_keyboard_update(&state, bytes, sizeof(bytes), &layouts, &input, UINT32_MAX);
      if (result.event) {
        if (result.event.value == 8) {
          if (result.event.longPress)
            expected.clear();
          else if (!expected.empty())
            expected.pop_back();
        } else {
          const char* out = result.event.longPress ? ui::keyboardAltOutputFor(layer, result.event.value) : nullptr;
          if (!out) out = ui::keyboardOutputFor(layer, result.event.value);
          assert(out);
          expected += out;
        }
      }
      if (effect.haptics != (result.event ? result.event.longPress ? 2 : 1 : 0)) {
        printf(
            "seed=%u step=%u kind=%u key=%u now=%u sdkEvent=%u value=%d long=%d rustHaptic=%u rustActive=%d "
            "sdkActive=%d length=%u expected=%zu\n",
            seed, step, kind, key, now, bool(result.event), result.event.value, result.event.longPress, effect.haptics,
            state.active_key, hits.activeIndex(), state.length, expected.size());
        fflush(stdout);
        return 1;
      }
      assert(state.length == expected.size() && state.cursor == expected.size());
      assert(!memcmp(bytes, expected.c_str(), expected.size() + 1));
      const auto active = hits.activeIndex();
      assert(state.active_key == (active < 0 ? INT16_MIN : hits.publishedData()[active].value));
    }
  }
  puts("16384 SDK TouchHoldRouter versus Rust frames: taps, holds, slides, slop, cancellation and timer wrap match");
}
