#include <FreeInkUI.h>

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "keyboard-layout-bindings.inc"
#include "xp_ui_keyboard.h"
namespace ui = freeink::ui;
static_assert(sizeof(XpUiKeyboardState) == 64 && offsetof(XpUiKeyboardState, active_index) == 62);
static bool oversized = false;
static XpUiKeyboardLayouts layouts = ProductionKeyboardLayouts::layouts();
static auto originalKey = layouts.key;
int main() {
  layouts.key = [](void* p, uint8_t id, uint8_t sh, uint8_t sy, uint8_t la, uint8_t r, uint8_t c) {
    auto key = originalKey(p, id, sh, sy, la, r, c);
    if (oversized) key.label = "01234567890123456789";
    return key;
  };
  size_t longest = 0, alt = 0;
  for (uint8_t id = 0; id < 10; id++)
    for (uint8_t shift = 0; shift < 2; shift++)
      for (uint8_t symbols = 0; symbols < 2; symbols++)
        for (uint8_t language = 0; language < 2; language++) {
          XpUiKeyboardState s{};
          s.layout = id;
          s.shifted = shift;
          s.symbols = symbols;
          s.show_language = language;
          const auto& l = ui::builtinKeyboardLayout(keyboard_layouts::ALL[id].id, shift, symbols, true, language);
          unsigned keys = 0;
          for (uint8_t r = 0; r < l.rowCount; ++r) keys += l.rows[r].count;
          assert(keys <= 50);
          for (uint8_t r = 0; r < l.rowCount; r++)
            for (uint8_t c = 0; c < l.rows[r].count; c++) {
              const auto& k = l.rows[r].keys[c];
              longest = std::max(longest, k.label ? strlen(k.label) : 0);
              alt = std::max(alt, k.alt ? strlen(k.alt) : 0);
              XpUiKeyboardPaintKey out{};
              assert(xp_ui_keyboard_paint_key(&s, &layouts, r, c, &out));
              assert(out.value == k.value);
            }
        }
  XpUiKeyboardState state{};
  state.active_key = INT16_MIN;
  state.enabled_layouts = 1;
  XpUiKeyboardPaintKey key{};
  memset(&key, 0xa5, sizeof(key));
  const auto prior = key;
  oversized = true;
  assert(!xp_ui_keyboard_paint_key(&state, &layouts, 0, 0, &key));
  assert(!memcmp(&prior, &key, sizeof(key)));
  oversized = false;
  assert(!xp_ui_keyboard_paint_key(&state, &layouts, 5, 0, &key));
  assert(!xp_ui_keyboard_paint_key(&state, &layouts, 0, 255, &key));
  XpUiKeyboardViewLayout l{};
  l.screen = {0, 0, 528, 792};
  l.content = l.screen;
  l.font = 1;
  l.small_font = 0;
  l.spacing = 8;
  l.header_height = 40;
  l.key_height = 56;
  l.hints_height = 40;
  l.key_spacing = 6;
  l.text_percent = 80;
  l.field_normal = 2;
  l.field_cursor = 3;
  l.field_end = -1;
  XpUiKeyboardMetrics metrics{
      nullptr,
      [](void*, int32_t, const uint8_t*, uint32_t n, uint8_t mode) -> int32_t { return mode == 2 ? 0 : n * 8; },
      [](void*, int32_t) -> int32_t { return 20; }};
  XpUiCommand commands[80];
  XpUiInteraction hits[56];
  uint8_t bytes[1024];
  XpUiKeyboardTextSpan spans[64]{};
  XpUiKeyboardBuffer b{commands, hits, bytes, 80, 56, 1024, spans, 64};
  XpUiScene scene{};
  uint8_t raw[] = {0};
  assert(xp_ui_keyboard_view(&state, raw, raw, &layouts, &l, &metrics, &b, &scene));
  assert(scene.interaction_count == 0);
  auto old = scene;
  oversized = true;
  assert(!xp_ui_keyboard_view(&state, raw, raw, &layouts, &l, &metrics, &b, &scene));
  assert(!memcmp(&old, &scene, sizeof(scene)));
  oversized = false;
  b.command_capacity = 1;
  assert(!xp_ui_keyboard_view(&state, raw, raw, &layouts, &l, &metrics, &b, &scene));
  assert(!memcmp(&old, &scene, sizeof(scene)));
  std::string zeroAdvance(10000, 'w');
  state.length = state.cursor = zeroAdvance.size();
  b.command_capacity = 80;
  metrics.measure = [](void*, int32_t, const uint8_t*, uint32_t, uint8_t) -> int32_t { return 0; };
  assert(xp_ui_keyboard_view(&state, reinterpret_cast<const uint8_t*>(zeroAdvance.data()),
                             reinterpret_cast<const uint8_t*>(zeroAdvance.data()), &layouts, &l, &metrics, &b, &scene));
  assert(spans[0].start == 0 && spans[0].length == 10000 && spans[0].raw == 0);
  assert(scene.text_len < 1024 && state.length == 10000 && zeroAdvance == std::string(10000, 'w'));
  old = scene;
  b.span_capacity = 0;
  assert(!xp_ui_keyboard_view(&state, reinterpret_cast<const uint8_t*>(zeroAdvance.data()),
                              reinterpret_cast<const uint8_t*>(zeroAdvance.data()), &layouts, &l, &metrics, &b,
                              &scene));
  assert(!memcmp(&old, &scene, sizeof(scene)));
  printf("max SDK label %zu bytes, alternate %zu; oversized key and scene capacity reject atomically\n", longest, alt);
}
