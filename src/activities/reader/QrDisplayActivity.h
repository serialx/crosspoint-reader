#pragma once
#include <string>

#include "activities/util/UtilityActivity.h"
class QrDisplayActivity final : public UtilityActivity {
  std::string textPayload;
  const char* utilityText(uint32_t) const override;
  void utilityHints() const override;
  bool utilityResourceValid(uint32_t) const override;
  void utilityResource(const XpUiCommand&) override;

 public:
  explicit QrDisplayActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& payload)
      : UtilityActivity("QrDisplay", renderer, input, XP_UI_UTILITY_QR, 4, 0), textPayload(payload) {}
};
