#pragma once
#include "UtilityActivity.h"
class FrontlightPanelActivity final : public UtilityActivity {
  char brightnessText[8]{}, warmthText[8]{};
  const char* utilityText(uint32_t) const override;
  UtilityView utilityView() const override { return xp_ui_utility_frontlight_view; }
  void prepareUtilityView() override;
  void utilityLayout(XpUiUtilityLayout&) const override;
  void utilityHints() const override {}
  bool utilityResourceValid(uint32_t) const override;
  void utilityResource(const XpUiCommand&) override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyUtilityEffect(const XpUiUtilityEffect&) override;

 public:
  explicit FrontlightPanelActivity(GfxRenderer& renderer, MappedInputManager& input)
      : UtilityActivity("FrontlightPanel", renderer, input, XP_UI_UTILITY_FRONTLIGHT, 43, 11) {}
  void onEnter() override;
  void onExit() override;
  bool handleHomeGesture() override {
    finish();
    return true;
  }
};
