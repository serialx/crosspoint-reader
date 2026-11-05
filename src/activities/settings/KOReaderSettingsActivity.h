#pragma once
#include "SettingsUiActivity.h"
#include "components/OptionPopup.h"

class KOReaderSettingsActivity final : public SettingsUiActivity {
 public:
  explicit KOReaderSettingsActivity(GfxRenderer&, MappedInputManager&);
  static constexpr int MENU_ITEMS = 12;
  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup;
  bool discardPopup = false;
  void rustSceneFailed() override {
    SettingsUiActivity::rustSceneFailed();
    discardPopup = true;
  }
  int listCount() const override { return MENU_ITEMS; }
  const SettingsUiRow* rowAt(int index) const override {
    return index >= 0 && index < MENU_ITEMS ? &rowItems[index] : nullptr;
  }
  void prepareRows() override;
  void activateIndex(int) override;
  bool handleCustomInput() override;
  bool smallLabelsOnTouch() const override { return true; }
  const char* headerTitle() const override;
  std::string rowValues[MENU_ITEMS];
  SettingsUiRow rowItems[MENU_ITEMS]{};
};
