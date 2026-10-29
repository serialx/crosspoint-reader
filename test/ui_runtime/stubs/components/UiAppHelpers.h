#pragma once
#include "Activity.h"
struct TouchSnapshot {
  int16_t touchX = 10, touchY = 10;
  bool touchPressed, touchReleased, touchHeld;
};
inline TouchSnapshot touchSnapshotFrom(const MappedInputManager& input) {
  return {10, 10, bool(input.touchFlags & 1), bool(input.touchFlags & 2), bool(input.touchFlags & 4)};
}
namespace haptic_feedback {
inline void touchAction(bool) {}
}  // namespace haptic_feedback
