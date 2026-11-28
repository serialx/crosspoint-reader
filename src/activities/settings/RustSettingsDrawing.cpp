#include <FreeInkUIGfxRenderer.h>
#include <xp_settings_shell.h>

#include "activities/ActivityManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

void xp_settings_shell_snapshot(XpUiSettingsLayout* layout) {
  auto& renderer = activityManager.renderer;
  const auto& input = activityManager.mappedInput;
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto target = makeUiTarget(renderer);
  const auto device = target.deviceContext();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  *layout = {};
  auto& style = layout->list;
  style.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                  static_cast<int16_t>(renderer.getScreenHeight())};
  style.area = {device.safeArea.left, device.safeArea.top,
                static_cast<int16_t>(device.width - device.safeArea.left - device.safeArea.right),
                static_cast<int16_t>(device.height - device.safeArea.top - device.safeArea.bottom)};
  style.font = fonts.bodyFontId;
  style.row_height = input.hasTouch() ? 0 : m.listRowHeight;
  style.row_gap = m.listRowGap;
  style.inset = m.listInset;
  style.padding = m.listSidePadding;
  style.scroll_width = m.listScrollWidth;
  style.radius = m.listRowRadius;
  style.selection = m.listSelectionStyle;
  style.scroll_left = m.listScrollSide;
  style.bold = m.listTitleBold;
  layout->safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                  static_cast<int16_t>(safe.height)};
  layout->small_font = fonts.smallFontId;
  layout->fixed_font = UI_12_FONT_ID;
  layout->help_font = UI_10_FONT_ID;
  layout->top_padding = m.topPadding;
  layout->header_height = m.headerHeight;
  layout->hints_height = m.buttonHintsHeight;
  layout->spacing = m.verticalSpacing;
  layout->tab_height = m.tabBarHeight;
  layout->preview_height = UITheme::getInstance().getStatusBarHeight();
  layout->side_padding = m.contentSidePadding;
  layout->kind = XP_UI_SETTINGS_ROOT;
  layout->touch = input.hasTouch();
}

void xp_settings_shell_paint(const XpUiCommand* command, XpUiRect clip, const char* title, uint8_t icon) {
  auto& renderer = activityManager.renderer;
  const auto r = command->rect;
  if (icon == 0) {
    GUI.drawHeader(renderer, Rect{r.x, r.y, r.w, r.h}, title, CROSSPOINT_VERSION);
    return;
  }
  static constexpr const freeink::Icon* icons[] = {&icon_settings_32, &icon_smartphone_32, &icon_book_open_32,
                                                   &icon_pointer_32,  &icon_library_32,    &icon_wifi_32};
  if (icon > 6) return;
  const auto old = renderer.getClipRect();
  renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
  auto target = makeUiTarget(renderer);
  target.bitmap({r.x, r.y, r.w, r.h}, freeink::ui::bitmapFromIcon(*icons[icon - 1]), freeink::ui::BitmapMode::Contain,
                freeink::ui::Paint::solid(command->paint == 1 ? freeink::ui::Color::White : freeink::ui::Color::Black));
  renderer.setClipRect(old[0], old[1], old[2], old[3]);
}

void xp_settings_shell_theme_reload(void) { UITheme::getInstance().reload(); }
