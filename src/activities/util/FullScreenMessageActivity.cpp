#include "FullScreenMessageActivity.h"

#include "fontIds.h"
XpDialogLaunch FullScreenMessageActivity::launch() const {
  XpDialogLaunch out{};
  out.kind = XP_DIALOG_MESSAGE;
  out.style = style;
  out.message_font = UI_10_FONT_ID;
  out.body = {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
  switch (refreshMode) {
    case HalDisplay::FAST_REFRESH:
      out.refresh = XP_UI_FAST;
      break;
    case HalDisplay::HALF_REFRESH:
      out.refresh = XP_UI_HALF;
      break;
    case HalDisplay::FULL_REFRESH:
      out.refresh = XP_UI_FULL;
      break;
  }
  return out;
}
