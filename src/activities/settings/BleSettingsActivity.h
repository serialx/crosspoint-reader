#pragma once

#include <BlePageTurner.h>
#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "SettingsUiActivity.h"

class MappedInputManager;

// Bluetooth page turner: switch it on, scan, pair a remote, forget one, and map page/chapter buttons.
// The radio and the remote belong to lib/BlePageTurner; this screen only asks it.
class BleSettingsActivity final : public SettingsUiActivity {
 public:
  explicit BleSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : SettingsUiActivity("BleSettings", renderer, mappedInput, XP_UI_SETTINGS_BLE) {}

  void onEnter() override;
  void loop() override;

 private:
  int listCount() const override { return static_cast<int>(rows.size()); }
  const SettingsUiRow* rowAt(int i) const override { return i >= 0 && i < listCount() ? &rows[i] : nullptr; }
  void prepareRows() override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  XpUiSettingsBle state{};
  void onBackButton() override;
  void showView(uint8_t target);
  uint32_t signature() const;
  void refresh();
  // Button mapping reads the linked remote's button edges and stores them in its table.
  void startLearning(int index);
  void stepLearning();
  void keepLearned(bool hold);
  void onRowLongPress(int index) override;
  void clearMapping(int index);

  // Written on the main loop under the render lock, read while painting on the render task.
  std::vector<SettingsUiRow> rows;
  std::vector<std::string> labels;
  std::string status;
  char learnAddr[18]{};
  char mappingValues[8][24]{};
  const char* learningNote() const;
};
