#pragma once

#include <BoardConfig.h>
#include <xp_hal.h>

#include <cstdint>

#if FREEINK_CAP_HAPTIC && !CROSSPOINT_EMULATED
#include <HapticManager.h>
#endif

// Application-owned feedback policy; the SDK owns motor timing and PWM.
class HalHaptics {
#if FREEINK_CAP_HAPTIC && !CROSSPOINT_EMULATED
  static bool dispatch(XpHalHapticOp op, uint8_t value) {
    auto& motor = HapticManager::getInstance();
    switch (op) {
      case XP_HAL_HAPTIC_ENABLE:
        motor.setEnabled(value != 0);
        break;
      case XP_HAL_HAPTIC_INTENSITY:
        motor.setIntensity(value);
        break;
      case XP_HAL_HAPTIC_BEGIN:
        return motor.begin();
      case XP_HAL_HAPTIC_PULSE:
        motor.pulse();
        break;
      case XP_HAL_HAPTIC_PATTERN: {
        static constexpr HapticManager::Step pattern[] = {{45, 255}, {55, 0}, {45, 255}};
        motor.play(pattern, 3);
        break;
      }
    }
    return true;
  }
#endif

 public:
  static void longPress(bool enabled, uint8_t intensity) {
#if FREEINK_CAP_HAPTIC && !CROSSPOINT_EMULATED
    xp_hal_haptic(dispatch, true, enabled, false, intensity);
#else
    (void)enabled;
    (void)intensity;
#endif
  }

  static void feedback(bool enabled, bool pressed, uint8_t intensity) {
#if FREEINK_CAP_HAPTIC && !CROSSPOINT_EMULATED
    xp_hal_haptic(dispatch, false, enabled, pressed, intensity);
#else
    (void)enabled;
    (void)pressed;
    (void)intensity;
#endif
  }
};
