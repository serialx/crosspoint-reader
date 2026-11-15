#include "ConfirmationActivity.h"

#include <I18n.h>

#include "components/UITheme.h"
ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& input, const std::string& heading,
                                           const std::string& body, const std::string& cancelLabel,
                                           const std::string& confirmLabel)
    : UtilityActivity("Confirmation", renderer, input, XP_UI_UTILITY_CONFIRMATION, 10, 3),
      heading(heading),
      body(body),
      cancelLabel(cancelLabel),
      confirmLabel(confirmLabel) {}
const char* ConfirmationActivity::utilityText(const uint32_t h) const {
  switch (h) {
    case Title:
      return heading.c_str();
    case Body:
      return body.c_str();
    case Cancel:
      return cancelLabel.empty() ? tr(STR_CANCEL) : cancelLabel.c_str();
    case Confirm:
      return confirmLabel.empty() ? tr(STR_CONFIRM) : confirmLabel.c_str();
    default:
      return UtilityActivity::utilityText(h);
  }
}
void ConfirmationActivity::utilityHints() const {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
