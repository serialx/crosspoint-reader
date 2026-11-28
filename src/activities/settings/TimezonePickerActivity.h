#pragma once
#include <string>
#include <vector>

#include "SettingsUiActivity.h"

// Timezone selection list (src/util/Timezones.cpp table). Selecting an entry
// persists it, re-applies the clock's TZ rule, and returns.
class TimezonePickerActivity final : public SettingsUiActivity {
 public:
  explicit TimezonePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override;
  const SettingsUiRow* rowAt(int i) const override { return i >= 0 && i < listCount() ? &rowItems_[i] : nullptr; }
  void prepareRows() override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::vector<std::string> offsetLabels_;
  std::vector<SettingsUiRow> rowItems_;
};
