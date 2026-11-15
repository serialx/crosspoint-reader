#pragma once
#include <I18n.h>

#include "SettingsUiActivity.h"

class ButtonRemapActivity final : public SettingsUiActivity {
 public:
  ButtonRemapActivity(GfxRenderer& renderer, MappedInputManager& input)
      : SettingsUiActivity("ButtonRemap", renderer, input, XP_UI_SETTINGS_REMAP) {}
  void onEnter() override;
  void loop() override;
  void cancelRejectedInput(const XpUiInput& input) override {
    if (input.pressed & XP_UI_DOWN) finish();
  }

 private:
  XpUiSettingsRemap remap{};
  mutable SettingsUiRow row{};
  int listCount() const override { return 4; }
  void activateIndex(int) override {}
  const char* headerTitle() const override { return tr(STR_REMAP_FRONT_BUTTONS); }
  const SettingsUiRow* rowAt(int) const override;
  uint8_t screenFlags() const override { return remap.error; }
  const char* specialText(uint32_t) const override;
  void drawFooter() override;
  static const char* roleName(uint8_t);
  static const char* hardwareName(uint8_t);
};
