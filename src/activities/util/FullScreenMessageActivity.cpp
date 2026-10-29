#include "FullScreenMessageActivity.h"

#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();
  XpUiCommand commands[2]{};
  const auto metrics = rustMetrics();
  const XpUiRect screen{0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                        static_cast<int16_t>(renderer.getScreenHeight())};
  if (text.size() >= XP_UI_MAX_TEXT ||
      !xp_ui_message_view(&metrics, screen, UI_10_FONT_ID, style, reinterpret_cast<const uint8_t*>(text.c_str()),
                          text.size(), commands, 2)) {
    LOG_ERR("RUI", "Cannot build full-screen message");
    return;
  }
  XpUiScene scene{commands, reinterpret_cast<const uint8_t*>(text.c_str()), nullptr, 0,
                  2,        static_cast<uint16_t>(text.size() + 1),         0,       XP_UI_NONE,
                  0};
  switch (refreshMode) {
    case HalDisplay::FAST_REFRESH:
      scene.refresh = XP_UI_FAST;
      break;
    case HalDisplay::HALF_REFRESH:
      scene.refresh = XP_UI_HALF;
      break;
    case HalDisplay::FULL_REFRESH:
      scene.refresh = XP_UI_FULL;
      break;
  }
  paintRustScene(scene);
}
