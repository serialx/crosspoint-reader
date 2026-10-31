#pragma once

#include <I18n.h>

#include "CatalogUiActivity.h"
#include "network/HttpDownloader.h"

// Network catalogs share input, child screens, and transfer lifecycle. Each
// catalog supplies its own feed/auth protocol and navigation destinations.
class CatalogActivity : public CatalogUiActivity {
 public:
  enum class State {
    CHECK_WIFI,
    WIFI_SELECTION,
    SEARCH_INPUT,
    LOADING,
    BROWSING,
    DOWNLOADING,
    ERROR,
    PLUGIN_PICKER,
    LIST_PICKER,
    DONE,
    NO_TOKEN,
    AUTH
  };
  CatalogActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                  XpUiCatalogKind kind = XP_UI_CATALOG_BROWSER)
      : CatalogUiActivity(name, renderer, input, kind) {
    if (kind != XP_UI_CATALOG_BROWSER) xp_ui_catalog_mode(&ui, XP_UI_CATALOG_LOADING);
  }
  void onExit() override;

 protected:
  State getState() const { return static_cast<State>(ui.mode); }
  void setState(State value) {
    ResourceLock resources(*this);
    ModelLock lock(*this);
    xp_ui_catalog_mode(&ui, static_cast<uint8_t>(value));
  }
  std::string errorMessage, statusMessage;
  size_t downloadProgress = 0, downloadTotal = 0;
  bool cancelDownload = false;

  virtual void startBrowse() = 0;
  virtual void retryBrowse() { startBrowse(); }
  bool hasSearch() const override = 0;
  virtual void performSearch(const std::string& query) = 0;
  virtual void downloadFinished(bool cancelled) = 0;
  virtual void wifiSelectionCancelled();
  // Initial keyboard text when search opens (refining the active query).
  virtual std::string searchPrefill() const { return {}; }

  void catalogEffect(const XpUiEffect&) override;
  const char* catalogText(uint32_t) const override;
  // Only work in progress keeps the device awake: a connection, fetch,
  // transfer, sign-in poll, or child flow (Wi-Fi picker, search keyboard).
  // Settled screens (lists, pickers, errors, done, not signed in) may auto-sleep.
  bool preventAutoSleep() override { return xp_ui_catalog_awake(&ui); }
  static bool wifiConnected();
  void fail(StrId message);
  void beginLoading();
  void checkAndConnectWifi();
  void launchWifiSelection();
  void launchSearch();
  void beginDownload(const std::string& title);
  void finishDownload(HttpDownloader::DownloadError result);
  // postBody non-null turns the transfer into a POST of that body (e.g. an
  // LCP license to the fulfillment service), with postContentType as its
  // Content-Type. Same heap gating and cancel/progress pump either way.
  HttpDownloader::DownloadError downloadFile(const std::string& url, const std::string& dest,
                                             const std::string& user = {}, const std::string& password = {},
                                             const std::vector<HttpDownloader::Header>& headers = {},
                                             const std::string* postBody = nullptr,
                                             const char* postContentType = nullptr);

 private:
  void onDownloadProgress(size_t downloaded, size_t total);
  // Download input consumes home gestures before ActivityManager sees them.
  bool goHomeAfterCancel = false;
  mutable char bytesText[24]{};
};
