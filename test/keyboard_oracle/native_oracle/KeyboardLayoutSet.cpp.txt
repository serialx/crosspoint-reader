#include "KeyboardLayoutSet.h"

#include <xp_ui_keyboard.h>

#include "CrossPointSettings.h"

namespace keyboard_layouts {
uint16_t enabled() {
  return xp_ui_keyboard_enabled(SETTINGS.keyboardLayouts, static_cast<uint8_t>(I18N.getLanguage()));
}
freeink::ui::KeyboardLayoutId startingLayout() {
  return ALL[xp_ui_keyboard_start(enabled(), static_cast<uint8_t>(I18N.getLanguage()))].id;
}
freeink::ui::KeyboardLayoutId next(const freeink::ui::KeyboardLayoutId current) {
  uint8_t index = 0;
  for (uint8_t i = 0; i < COUNT; ++i) {
    if (ALL[i].id == current) {
      index = i;
      break;
    }
  }
  return ALL[xp_ui_keyboard_next(enabled(), index)].id;
}
}  // namespace keyboard_layouts
