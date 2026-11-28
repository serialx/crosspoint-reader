#pragma once

#include <HalGPIO.h>

#include "CrossPointSettings.h"

// The switch owns the state of its assigned feature.
inline bool slideSwitchOwns(const CrossPointSettings::SLIDE_SWITCH_ACTION action) {
  return gpio.hasToggleSwitch() && SETTINGS.slideSwitchAction == action;
}

inline bool slideSwitchOwnsLight() { return slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_FRONTLIGHT); }
