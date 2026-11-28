// Frozen settings policy methods from bcdfb338; deterministic service leaves.
#include <cstdint>
#include <cstdio>
#include <string>
namespace keyboard_layouts {
constexpr unsigned LATIN_BITS = 15;
constexpr uint16_t bitAt(uint8_t i) { return uint16_t(1u << i); }
}  // namespace keyboard_layouts
struct CrossPointSettings {
  enum QUICK_RESUME_SLEEP_SCREEN { QUICK_RESUME_NEVER, QUICK_RESUME_AFTER_TIMEOUT };
  enum SLEEP_SCREEN_MODE { QUICK_RESUME = 9 };
  uint8_t sleepScreen = 0, quickResumeSleepScreen = 0;
} SETTINGS;
struct KeyboardLayoutsActivity {
  uint16_t workingMask = 0;
  bool isLocked(uint8_t i) const;
};
struct SettingsActivity {
  bool preserveQuickResumeTimeoutOn = false, quickResumeTimeoutAutoEnabled = false;
  void syncQuickResumeTimeoutForSleepScreen(bool, bool);
};
constexpr int kRoleCount = 4;
constexpr uint32_t kErrorDisplayMs = 1500;
uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
constexpr int STR_ALREADY_ASSIGNED = 1;
const char* tr(int) { return "Already assigned"; }
struct ButtonRemapActivity {
  uint8_t tempMapping[4] = {255, 255, 255, 255}, currentStep = 0;
  uint32_t errorUntil = 0;
  std::string errorMessage;
  bool validateUnassigned(uint8_t);
};
bool KeyboardLayoutsActivity::isLocked(const uint8_t i) const {
  const uint16_t bit = keyboard_layouts::bitAt(i);
  if (!(workingMask & bit)) return false;
  const uint16_t without = static_cast<uint16_t>(workingMask & ~bit);
  return (without & keyboard_layouts::LATIN_BITS) == 0;
}
void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}
bool ButtonRemapActivity::validateUnassigned(const uint8_t pressedButton) {
  // Block reusing a hardware button already assigned to another role.
  for (uint8_t i = 0; i < kRoleCount; i++) {
    if (tempMapping[i] == pressedButton && i != currentStep) {
      errorMessage = tr(STR_ALREADY_ASSIGNED);
      errorUntil = millis() + kErrorDisplayMs;
      return false;
    }
  }
  return true;
}
int main() {
  KeyboardLayoutsActivity layouts;
  for (unsigned mask = 0; mask < 1024; ++mask) {
    layouts.workingMask = mask;
    unsigned locked = 0;
    for (unsigned i = 0; i < 10; ++i)
      if (layouts.isLocked(i)) locked |= 1u << i;
    std::printf("layout %u %u\n", mask, locked);
  }
  for (unsigned preserve = 0; preserve < 2; ++preserve)
    for (unsigned automatic = 0; automatic < 2; ++automatic)
      for (unsigned screen = 0; screen < 2; ++screen)
        for (unsigned timeout = 0; timeout < 3; ++timeout)
          for (unsigned flags = 0; flags < 4; ++flags) {
            SettingsActivity s;
            s.preserveQuickResumeTimeoutOn = preserve;
            s.quickResumeTimeoutAutoEnabled = automatic;
            SETTINGS.sleepScreen = screen ? CrossPointSettings::QUICK_RESUME : 0;
            SETTINGS.quickResumeSleepScreen = timeout;
            s.syncQuickResumeTimeoutForSleepScreen(flags & 1, flags & 2);
            std::printf("resume %u %u %u %u %u %u %u %u\n", preserve, automatic, screen, timeout, flags,
                        SETTINGS.quickResumeSleepScreen, s.preserveQuickResumeTimeoutOn,
                        s.quickResumeTimeoutAutoEnabled);
          }
  for (unsigned step = 0; step < 4; ++step)
    for (unsigned button = 0; button < 4; ++button) {
      ButtonRemapActivity s;
      s.currentStep = step;
      for (unsigned i = 0; i < step; ++i) s.tempMapping[i] = i;
      nowMs = 100;
      auto valid = s.validateUnassigned(button);
      std::printf("remap %u %u %u %u [%s]\n", step, button, valid, s.errorUntil, s.errorMessage.c_str());
    }
  return 0;
}
