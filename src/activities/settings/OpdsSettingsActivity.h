#pragma once

#include "OpdsServerStore.h"
#include "activities/CatalogUiActivity.h"

/**
 * Edit screen for a single OPDS server.
 * Shows Name, URL, Username, Password fields and a Delete option.
 * Used for both adding new servers and editing existing ones.
 */
class OpdsSettingsActivity final : public CatalogUiActivity {
 public:
  /**
   * @param serverIndex Index into OpdsServerStore, or -1 for a new server
   */
  explicit OpdsSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1);

  void onEnter() override;

 private:
  OpdsServer editServer;

  int listCount() const override { return getMenuItemCount(); }
  void prepareRows() override;
  bool rowAt(int index, XpUiCatalogRow& row) const override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  int getMenuItemCount() const;
  void handleSelection();
  bool saveServer();

  std::string headerDisplayBuf[OpdsServer::MAX_CUSTOM_HEADERS];
};
