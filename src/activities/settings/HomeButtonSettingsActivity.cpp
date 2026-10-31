#include "HomeButtonSettingsActivity.h"

#include <utility>

#include "components/UITheme.h"

namespace fui = freeink::ui;

bool HomeButtonSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void HomeButtonSettingsActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount() || optionPopup.isActive()) return;
  mappedInput.resetHomeButtonInput();

  selectIndex(index);
  const auto field = home_button::FIELDS[index];
  const uint8_t value = SETTINGS.*field;
  const int current = value < static_cast<uint8_t>(HomeButtonAction::Count) ? value : 0;
  optionPopup.show(home_button::GESTURE_LABELS[index], home_button::ACTION_LABELS,
                   static_cast<int>(HomeButtonAction::Count), current, [field](int selected) {
                     SETTINGS.*field = static_cast<uint8_t>(selected);
                     SETTINGS.saveToFile();
                   });
  requestUpdate();
}

void HomeButtonSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  SettingsUiActivity::render(std::move(lock));
}

void HomeButtonSettingsActivity::prepareRows() {
  for (int i = 0; i < listCount(); ++i) {
    rows[i].label = I18N.get(home_button::GESTURE_LABELS[i]);
    rows[i].actionValue = static_cast<int16_t>(i);
    const uint8_t value = SETTINGS.*home_button::FIELDS[i];
    rows[i].value = value < static_cast<uint8_t>(HomeButtonAction::Count) ? I18N.get(home_button::ACTION_LABELS[value])
                                                                          : I18N.get(home_button::ACTION_LABELS[0]);
  }
}
