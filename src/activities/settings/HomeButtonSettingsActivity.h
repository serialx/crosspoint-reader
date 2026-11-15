#pragma once

#include "HomeButtonSettings.h"
#include "SettingsUiActivity.h"
#include "components/OptionPopup.h"

class HomeButtonSettingsActivity final : public SettingsUiActivity {
 public:
  HomeButtonSettingsActivity(GfxRenderer& renderer, MappedInputManager& input)
      : SettingsUiActivity("HomeButtonSettings", renderer, input, XP_UI_SETTINGS_HOME_BUTTON) {}

 private:
  static constexpr int GESTURE_COUNT = 3;

  SettingsUiRow rows[GESTURE_COUNT]{};
  OptionPopup optionPopup;

  int listCount() const override { return GESTURE_COUNT; }
  const char* headerTitle() const override { return tr(STR_HOME_BUTTON); }
  const SettingsUiRow* rowAt(int i) const override { return i >= 0 && i < listCount() ? &rows[i] : nullptr; }
  void prepareRows() override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void render(RenderLock&&) override;
};
