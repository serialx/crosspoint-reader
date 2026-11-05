#pragma once

#include <xp_ui_reader.h>

inline bool remoteTurnAccepted(const bool pageShown, const bool rendering, const bool inputOverPage) {
  return xp_ui_reader_remote_allowed(pageShown, rendering, inputOverPage);
}
