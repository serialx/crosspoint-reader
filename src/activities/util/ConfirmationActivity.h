#pragma once
#include "DialogActivity.h"
class ConfirmationActivity : public DialogActivity {
  std::string heading, body, cancelLabel, confirmLabel;

 public:
  ConfirmationActivity(GfxRenderer&, MappedInputManager&, const std::string& heading, const std::string& body,
                       const std::string& cancelLabel = {}, const std::string& confirmLabel = {});
  XpDialogLaunch launch() const override;
  void releaseLaunch() override;
};
