#pragma once

#include <string>

#include "UpdateUiActivity.h"

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> push FileBrowserActivity in PickFirmware mode (only .bin files visible).
 *  2) On result: validate the .bin (header magic, size fits OTA partition).
 *  3) Push ConfirmationActivity ("Update firmware?").
 *  4) On confirm: stream the file into the OTA partition via the Arduino Update API,
 *     drawing a progress bar; on success ESP.restart().
 *
 * Used both from Settings -> System -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (left side button + power on X3).
 */
class SdFirmwareUpdateActivity : public UpdateUiActivity {
 public:
  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false)
      : UpdateUiActivity("SdFirmwareUpdate", renderer, mappedInput, XP_UI_UPDATE_SD, recoveryMode) {}

  void onEnter() override;

 private:
  void performAction(XpUiUpdateAction, int32_t) override;
  const char* dynamicText(uint32_t) const override;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  std::string errorMessage;

  void launchPicker();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
