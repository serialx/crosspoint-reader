#include "KeyboardLayoutSet.h"

#include <xp_ui_keyboard.h>

#include "CrossPointSettings.h"

namespace keyboard_layouts {
uint16_t enabled() {
  return xp_ui_keyboard_enabled(SETTINGS.keyboardLayouts, static_cast<uint8_t>(I18N.getLanguage()));
}
freeink::ui::KeyboardLayoutId startingLayout() {
  return static_cast<freeink::ui::KeyboardLayoutId>(
      xp_ui_keyboard_start(enabled(), static_cast<uint8_t>(I18N.getLanguage())));
}
freeink::ui::KeyboardLayoutId next(const freeink::ui::KeyboardLayoutId current) {
  return static_cast<freeink::ui::KeyboardLayoutId>(xp_ui_keyboard_next(enabled(), static_cast<uint8_t>(current)));
}
}  // namespace keyboard_layouts
