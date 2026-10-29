#pragma once
#include <string>

#include "UtilityActivity.h"
class ConfirmationActivity : public UtilityActivity {
  std::string heading, body, cancelLabel, confirmLabel;
  const char* utilityText(uint32_t) const override;
  void utilityHints() const override;

 public:
  ConfirmationActivity(GfxRenderer&, MappedInputManager&, const std::string& heading, const std::string& body,
                       const std::string& cancelLabel = {}, const std::string& confirmLabel = {});
};
