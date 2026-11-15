#include "KeyboardLayoutSet.h"

#include <xp_ui_keyboard.h>

#include "CrossPointSettings.h"

namespace keyboard_layouts {
uint16_t enabled() {
  return xp_ui_keyboard_enabled(SETTINGS.keyboardLayouts, static_cast<uint8_t>(I18N.getLanguage()));
}
}  // namespace keyboard_layouts
