#include "BootActivity.h"

#include <I18n.h>
void BootActivity::onEnter() {
  Activity::onEnter();
  paintBootView(XP_UI_BOOT_SPLASH, XP_UI_FAST, ViewData{tr(STR_BOOTING)});
}
