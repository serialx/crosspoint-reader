#pragma once

#include <GfxRenderer.h>

#include "SettingsUiActivity.h"
#include "activities/util/KeyboardLayoutSet.h"

class MappedInputManager;

class KeyboardLayoutsActivity final : public SettingsUiActivity {
 public:
  explicit KeyboardLayoutsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : SettingsUiActivity("KeyboardLayouts", renderer, mappedInput, XP_UI_SETTINGS_LAYOUTS) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return keyboard_layouts::COUNT; }
  const SettingsUiRow* rowAt(int i) const override { return i >= 0 && i < listCount() ? &rowItems[i] : nullptr; }
  void prepareRows() override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  bool isLocked(uint8_t i) const;

  SettingsUiRow rowItems[keyboard_layouts::COUNT]{};

  XpUiSettingsLayouts selection{};
  // Do not persist a derived default merely because the screen was visited.
};
