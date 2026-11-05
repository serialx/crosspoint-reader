#include "CrashActivity.h"

#include <GfxRenderer.h>
#include <HalSystem.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "fontIds.h"

void CrashActivity::onEnter() {
  Activity::onEnter();

  panicMessage = HalSystem::getPanicInfo(false);
  if (panicMessage.empty()) {
    panicMessage = tr(STR_CRASH_NO_REASON);
  }

  requestUpdateAndWait();
}

void CrashActivity::loop() {
  XpUiInput input{};
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) input.pressed = XP_UI_BACK;
  int x = 0, y = 0;
  input.action = !input.pressed && mappedInput.wasScreenTapped(x, y) ? 1 : 0;
  if (xp_ui_crash_update(&input).kind == XP_UI_CANCEL) finish();
}

void CrashActivity::render(RenderLock&&) {
  const int width = renderer.getScreenWidth() - 2 * UITheme::getInstance().getMetrics().contentSidePadding;
  const auto description = renderer.wrappedText(UI_10_FONT_ID, tr(STR_CRASH_DESCRIPTION), width, 10);
  const auto panic = renderer.wrappedText(UI_10_FONT_ID, panicMessage.c_str(), width, 5);
  paintBootView(XP_UI_BOOT_CRASH, XP_UI_FAST, ViewData{nullptr, nullptr, &description, &panic});
}
