#pragma once

#include <array>

#include "SettingsSection.h"
#include "SettingsUiActivity.h"

// Settings root: one row per SettingsSection, each opening a
// SettingsActivity with that group's settings.
class SettingsListActivity final : public SettingsUiActivity {
  // Section opened on the first loop pass (End = none), e.g. returning to
  // Network after the Wi-Fi rows' silent restart.
  SettingsSection pendingSection;

  std::array<SettingsUiRow, SETTINGS_SECTION_COUNT> rowItems_{};

  void rebuildRows();
  void openSection(SettingsSection section);

  int listCount() const override { return static_cast<int>(SETTINGS_SECTION_COUNT); }
  const SettingsUiRow* rowAt(int i) const override { return i >= 0 && i < listCount() ? &rowItems_[i] : nullptr; }
  void prepareRows() override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  const char* headerTrailing() const override;

 public:
  explicit SettingsListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                SettingsSection openSection = SettingsSection::End);
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
