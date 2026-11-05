#include "KeyboardLayoutsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void KeyboardLayoutsActivity::onEnter() {
  SettingsUiActivity::onEnter();
  if (!hasUiStorage()) return;
  selection.mask = keyboard_layouts::enabled();
  selection.edited = false;

  for (int i = 0; i < keyboard_layouts::COUNT; ++i) {
    rowItems[i].label = I18N.getLanguageName(keyboard_layouts::ALL[i].language);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void KeyboardLayoutsActivity::onExit() {
  if (selection.edited && selection.mask != SETTINGS.keyboardLayouts) {
    SETTINGS.keyboardLayouts = selection.mask;
    SETTINGS.saveToFile();
  }
  SettingsUiActivity::onExit();
}

const char* KeyboardLayoutsActivity::headerTitle() const { return tr(STR_KEYBOARD_LAYOUTS); }

bool KeyboardLayoutsActivity::isLocked(uint8_t i) const { return xp_ui_settings_layout_locked(&selection, i) != 0; }

void KeyboardLayoutsActivity::activateIndex(const int index) {
  selectIndex(index);
  // The row stays on screen with a new checkbox state; a lingering flash would
  // gray an unrelated row on the repaint below.

  xp_ui_settings_layout_toggle(&selection, static_cast<uint8_t>(index));
  requestUpdate();
}

void KeyboardLayoutsActivity::prepareRows() {
  for (int i = 0; i < keyboard_layouts::COUNT; ++i) {
    const uint8_t row = static_cast<uint8_t>(i);
    setCheckboxRow(rowItems[i], (selection.mask & keyboard_layouts::bitAt(row)) != 0);
    if (isLocked(row)) {
      rowItems[i].toggle = false;
      rowItems[i].value = tr(STR_DEFAULT_VALUE);
    }
  }
}
