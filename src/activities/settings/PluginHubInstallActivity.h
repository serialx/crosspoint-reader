#pragma once

#include "activities/CatalogActivity.h"

class PluginHubInstallActivity final : public CatalogActivity {
 public:
#ifdef CROSSPOINT_UI_ORACLE
  friend bool uiOracleHubEnter(PluginHubInstallActivity&);
#endif
  explicit PluginHubInstallActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    bool showConfirmation = true);

  static bool isAvailable();
  void onEnter() override;

 private:
  bool showConfirmation = true;

  int listCount() const override { return 0; }
  bool hasSearch() const override { return false; }
  void activateIndex(int) override {}
  bool rowAt(int, XpUiCatalogRow&) const override { return false; }
  const char* headerTitle() const override { return tr(STR_INSTALL_PLUGIN_HUB); }
  const char* catalogText(uint32_t) const override;
  void catalogEffect(const XpUiEffect&) override;
  void onBackButton() override;
  void startBrowse() override;
  void retryBrowse() override { startBrowse(); }
  void performSearch(const std::string&) override {}
  void downloadFinished(bool cancelled) override;
  void wifiSelectionCancelled() override;

  HttpDownloader::DownloadError installPluginHub();
  static void cleanupStagedFiles();
};
