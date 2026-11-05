#include "QrDisplayActivity.h"

#include <I18n.h>

#include "components/UITheme.h"
#include "util/QrUtils.h"
const char* QrDisplayActivity::utilityText(const uint32_t h) const {
  return h == Title ? tr(STR_DISPLAY_QR) : UtilityActivity::utilityText(h);
}
void QrDisplayActivity::utilityHints() const {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
bool QrDisplayActivity::utilityResourceValid(const uint32_t h) const {
  return h == Qr || UtilityActivity::utilityResourceValid(h);
}
void QrDisplayActivity::utilityResource(const XpUiCommand& c) {
  if (c.resource == Qr)
    QrUtils::drawQrCode(renderer, Rect{c.rect.x, c.rect.y, c.rect.w, c.rect.h}, textPayload);
  else
    UtilityActivity::utilityResource(c);
}
