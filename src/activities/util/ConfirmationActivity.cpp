#include "ConfirmationActivity.h"
ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& heading,
                                           const std::string& body, const std::string& cancelLabel,
                                           const std::string& confirmLabel)
    : DialogActivity("Confirmation", renderer, input),
      heading(heading),
      body(body),
      cancelLabel(cancelLabel),
      confirmLabel(confirmLabel) {}
XpDialogLaunch ConfirmationActivity::launch() const {
  const auto text = [](const std::string& s) {
    return XpAppText{reinterpret_cast<const uint8_t*>(s.data()), s.size()};
  };
  XpDialogLaunch out{};
  out.kind = XP_DIALOG_CONFIRMATION;
  out.heading = text(heading);
  out.body = text(body);
  out.cancel = text(cancelLabel);
  out.confirm = text(confirmLabel);
  return out;
}
void ConfirmationActivity::releaseLaunch() {
  std::string().swap(heading);
  std::string().swap(body);
  std::string().swap(cancelLabel);
  std::string().swap(confirmLabel);
}
