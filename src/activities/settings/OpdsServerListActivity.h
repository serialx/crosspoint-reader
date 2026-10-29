#pragma once

#include "activities/CatalogUiActivity.h"
#include "components/OptionPopup.h"

/**
 * Activity showing the list of configured OPDS servers.
 * Allows adding new servers and editing/deleting existing ones.
 * When pickerMode is true, selecting a server navigates to the OPDS browser
 * instead of opening the editor (used from the home screen).
 */
class OpdsServerListActivity final : public CatalogUiActivity {
 public:
  explicit OpdsServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool pickerMode = false);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return getItemCount(); }
  bool rowAt(int index, XpUiCatalogRow& row) const override;
  void activateIndex(int index) override;
  // Popup input goes first; while it is open it consumes the pass.
  bool handleCustomInput() override;
  // Picker mode backs out to the home menu rather than finishing.
  void onBackButton() override;
  const char* headerTitle() const override;

  OptionPopup optionPopup;

  int getItemCount() const;
  void handleSelection();
};
