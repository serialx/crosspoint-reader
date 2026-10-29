#pragma once
#include "SettingsUiActivity.h"

class ClockSyncActivity final : public SettingsUiActivity {
 public:
  ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& input)
      : SettingsUiActivity("ClockSync", renderer, input, XP_UI_SETTINGS_SYNC) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }

 private:
  char syncedTime[16]{};
  char currentTime[64]{};
  mutable SettingsUiRow row{};
  bool shouldTearDownWifiOnExit = false;
  void runSync();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool);
  int listCount() const override { return 4; }
  void activateIndex(int) override {}
  const char* headerTitle() const override { return tr(STR_CLOCK_SYNC); }
  const SettingsUiRow* rowAt(int) const override;
  void prepareRows() override;
  void drawFooter() override;
};
