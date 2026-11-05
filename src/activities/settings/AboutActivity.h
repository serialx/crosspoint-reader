#pragma once
#include <string>

#include "SettingsUiActivity.h"

// Read-only device information: detected hardware (device profile, display
// controller, touch, frontlight, RTC, IMU), firmware version, and MAC address.
// Rows are informational; activating one does nothing.
class AboutActivity final : public SettingsUiActivity {
 public:
  explicit AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 11;

  void onEnter() override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  const SettingsUiRow* rowAt(int i) const override { return i >= 0 && i < listCount() ? &rowItems_[i] : nullptr; }
  void prepareRows() override;
  void activateIndex(int) override {}
  const char* headerTitle() const override;

  std::string rowValues_[ITEM_COUNT];
  SettingsUiRow rowItems_[ITEM_COUNT]{};
};
