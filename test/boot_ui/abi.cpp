#include <xp_ui_boot.h>

#include <cassert>
#include <cstddef>
#include <cstring>

static_assert(XP_UI_BOOT_XTC_PIXELS == 12 && XP_UI_BOOT_XTC_PAGE == 8);
static_assert(sizeof(XpUiXtcDiagnostics) == 2 * sizeof(void*) && alignof(XpUiXtcDiagnostics) == alignof(void*) &&
              offsetof(XpUiXtcDiagnostics, outside) == sizeof(void*));
static_assert(sizeof(XpUiSleepPlan) == 8 && offsetof(XpUiSleepPlan, dark_default) == 5);
static_assert(alignof(XpUiImagePlacement) == 4 && offsetof(XpUiImagePlacement, crop_x) == 8 &&
              offsetof(XpUiImagePlacement, crop_y) == 12);
static_assert(sizeof(XpUiBootLayout) == 36 && offsetof(XpUiBootLayout, flags) == 32);
static_assert(sizeof(XpUiImageState) == 12 && offsetof(XpUiImageState, can_set_cover) == 8);
static_assert(sizeof(XpUiXtcState) == 4 && sizeof(XpUiImagePlacement) == 16);
static_assert(sizeof(XpUiXtcOverlay) == 16 && offsetof(XpUiXtcOverlay, draw) == 12);
static_assert(sizeof(XpUiBootMetrics) == 2 * sizeof(void*) && offsetof(XpUiBootMetrics, width) == sizeof(void*));
int main() {
  const auto p = xp_ui_sleep_plan(8, 0, 0, 1, 1);
  assert(p.route == XP_UI_SLEEP_COVER && p.fallback == XP_UI_SLEEP_OVERLAY);
  assert(p.preserve_frame && p.invert_frame && p.release_fonts && p.overlay_cover);
  XpUiImageState state{1, 3, 1, {}};
  XpUiInput input{};
  input.released = XP_UI_RIGHT;
  const auto effect = xp_ui_image_update(&state, &input);
  assert(effect.kind == XP_UI_SCROLL && state.selected == 2 && effect.value == 2);
  assert(xp_ui_image_hints(&state) == 3);
  XpUiXtcState page{2};
  assert(xp_ui_xtc_turn(&page, 3, 1, false) && page.page == 3);
  xp_ui_xtc_restore(&page, 3, true);
  assert(page.page == 2);
  XpUiCommand commands[19]{};
  XpUiBuffer buffer{commands, nullptr, 19, 0};
  XpUiBootLayout layout{};
  layout.screen = {0, 0, 528, 792};
  layout.view = XP_UI_BOOT_SPLASH;
  layout.line_height = 23;
  XpUiBootMetrics metrics{nullptr, [](void*, uint32_t, int32_t, uint8_t) -> int32_t { return 73; }};
  XpUiScene out{};
  assert(xp_ui_boot_view(&layout, &metrics, &buffer, &out));
  assert(out.command_count == 5 && commands[1].resource == XP_UI_BOOT_LOGO);
  assert(commands[2].rect.x == (528 - 73) / 2 && commands[2].rect.y == 466);
  layout.view = XP_UI_BOOT_XTC_PAGE;
  assert(xp_ui_boot_view(&layout, &metrics, &buffer, &out));
  assert(out.command_count == 1 && commands[0].resource == XP_UI_BOOT_XTC_PIXELS && out.refresh == XP_UI_NONE);
  const auto before = out;
  layout.view = 255;
  assert(!xp_ui_boot_view(&layout, &metrics, &buffer, &out));
  assert(std::memcmp(&before, &out, sizeof(out)) == 0);
  uint8_t pixels[4]{255, 255, 255, 255};
  const uint8_t pageBytes[6]{};
  XpGlyphTarget target{};
  target.buffer = pixels;
  target.bufferLen = sizeof(pixels);
  target.width = 8;
  target.height = 4;
  target.stride = 1;
  target.rows = 4;
  target.clip = {0, 0, 100, 100};
  target.orientation = 3;
  int outsideCount = 0;
  XpUiXtcDiagnostics diagnostics{&outsideCount, [](void* p, int32_t x, int32_t y, uint8_t black) {
                                   auto& count = *static_cast<int*>(p);
                                   assert(x == count % 8 && y == 4 + count / 8 && black == 1);
                                   ++count;
                                 }};
  assert(xp_ui_xtc_pixels(&target, pageBytes, sizeof(pageBytes), 8, 6, 1, 0, &diagnostics));
  assert(outsideCount == 16);
  for (const auto pixel : pixels) assert(pixel == 0);
}
