#pragma once
#include "UpdateUiActivity.h"
#include "network/OtaUpdater.h"
class OtaUpdateActivity : public UpdateUiActivity {
  OtaUpdater updater;
  const char* failedDetail = nullptr;
  std::string currentVersion, newVersion;
  char bytes[32]{};
  void onWifiSelectionComplete(bool);
  void runUpdateInstall();
  void performAction(XpUiUpdateAction, int32_t) override;
  void prepareText() override;
  const char* dynamicText(uint32_t) const override;
  void afterHeader() const override;

 public:
  OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UpdateUiActivity("OtaUpdate", renderer, input, XP_UI_UPDATE_OTA) {}
  void onEnter() override;
  void onExit() override;
};
