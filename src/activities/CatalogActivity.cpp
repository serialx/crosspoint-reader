#include "CatalogActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <FreeInkUIIcon.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UiAppHelpers.h"
#include "components/icons/search32.h"

static_assert(static_cast<int>(CatalogActivity::State::AUTH) == XP_UI_CATALOG_AUTH);

void CatalogActivity::onEnter() { CatalogUiActivity::onEnter(); }

void CatalogActivity::onExit() {
  CatalogUiActivity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

bool CatalogActivity::wifiConnected() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void CatalogActivity::fail(const StrId message) {
  ResourceLock resourceLock(*this);
  setState(State::ERROR);
  errorMessage = I18N.get(message);
  requestUpdate();
}

void CatalogActivity::beginLoading() {
  ResourceLock resourceLock(*this);
  setState(State::LOADING);
  statusMessage = tr(STR_LOADING);
  requestUpdate(true);
}

void CatalogActivity::checkAndConnectWifi() {
  if (wifiConnected())
    startBrowse();
  else
    launchWifiSelection();
}

void CatalogActivity::launchWifiSelection() {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("CAT", "OOM: Wi-Fi selection");
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  setState(State::WIFI_SELECTION);
  requestUpdate();
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled)
      wifiSelectionCancelled();
    else
      startBrowse();
  });
}

void CatalogActivity::wifiSelectionCancelled() { fail(StrId::STR_WIFI_CONN_FAILED); }

void CatalogActivity::launchSearch() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH), searchPrefill());
  if (!keyboard) {
    LOG_ERR("CAT", "OOM: search keyboard");
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  setState(State::SEARCH_INPUT);
  requestUpdate();
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    setState(State::BROWSING);
    if (!result.isCancelled)
      performSearch(std::get<KeyboardResult>(result.data).text);
    else
      requestUpdate();
  });
}

void CatalogActivity::catalogEffect(const XpUiEffect& effect) {
  switch (effect.kind) {
    case XP_UI_CATALOG_SEARCH:
      launchSearch();
      break;
    case XP_UI_CATALOG_RETRY:
      if (wifiConnected())
        retryBrowse();
      else
        launchWifiSelection();
      break;
    case XP_UI_CATALOG_CANCEL_TRANSFER:
      cancelDownload = true;
      break;
    default:
      break;
  }
}
const char* CatalogActivity::catalogText(uint32_t handle) const {
  switch (handle) {
    case 0x80000003:
      return statusMessage.c_str();
    case 0x80000006:
      return errorMessage.c_str();
    case 0x80000009:
      if (paintState.progress >= 1024 * 1024)
        snprintf(bytesText, sizeof(bytesText), "%u.%u MB", unsigned(paintState.progress >> 20),
                 unsigned((paintState.progress >> 10) % 1024 * 10 / 1024));
      else
        snprintf(bytesText, sizeof(bytesText), "%u KB", unsigned(paintState.progress >> 10));
      return bytesText;
    default:
      return CatalogUiActivity::catalogText(handle);
  }
}

void CatalogActivity::beginDownload(const std::string& title) {
  ResourceLock resourceLock(*this);
  setState(State::DOWNLOADING);
  statusMessage = title;
  downloadProgress = downloadTotal = 0;
  cancelDownload = goHomeAfterCancel = false;
  requestUpdate(true);
}

void CatalogActivity::onDownloadProgress(const size_t downloaded, const size_t total) {
  // Home cancels immediately; other configured actions are deferred to the
  // next main-loop pass by the transfer input pump.
  mappedInput.update(true);
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) cancelDownload = true;
  if (mappedInput.wasHomeGesture()) cancelDownload = goHomeAfterCancel = true;
  const auto touch = touchSnapshotFrom(mappedInput);
  if (touch.touchReleased) {
    // Reuse the Rust adapter's published hit routing during the transfer pump.
    RustActivityAdapter::loop();
  }
  const auto now = millis();
  bool redraw;
  {
    ModelLock lock(*this);
    downloadProgress = downloaded;
    downloadTotal = total;
    redraw = xp_ui_catalog_progress(&ui, downloaded, total, now);
  }
  if (redraw) requestUpdate(true);
}

HttpDownloader::DownloadError CatalogActivity::downloadFile(const std::string& url, const std::string& dest,
                                                            const std::string& user, const std::string& password,
                                                            const std::vector<HttpDownloader::Header>& headers,
                                                            const std::string* postBody, const char* postContentType) {
  downloadProgress = downloadTotal = 0;
  {
    ModelLock lock(*this);
    xp_ui_catalog_mode(&ui, XP_UI_CATALOG_DOWNLOADING);
  }
  // Rebuildable SD-font caches can hold tens of KB the TLS session needs for
  // a multi-MB file; release them up front (they repopulate on demand) and
  // refuse to start below the floor. A doomed transfer otherwise dies
  // mid-stream with MEMORY_E, or abort()s on an interior allocation.
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  if (ESP.getFreeHeap() < HttpDownloader::MIN_TLS_FREE_HEAP ||
      ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
    LOG_ERR("CAT", "Low heap for download (%u free, %u max block)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return HttpDownloader::HTTP_ERROR;
  }
  if (postBody) {
    return HttpDownloader::postToFile(
        url, *postBody, postContentType ? postContentType : "application/octet-stream", dest,
        [this](size_t downloaded, size_t total) { onDownloadProgress(downloaded, total); }, &cancelDownload);
  }
  return HttpDownloader::downloadToFile(
      url, dest, [this](size_t downloaded, size_t total) { onDownloadProgress(downloaded, total); }, &cancelDownload,
      user, password, headers);
}

void CatalogActivity::finishDownload(const HttpDownloader::DownloadError result) {
  if (result == HttpDownloader::ABORTED && goHomeAfterCancel) {
    onGoHome();
  } else if (result == HttpDownloader::OK || result == HttpDownloader::ABORTED) {
    downloadFinished(result == HttpDownloader::ABORTED);
  } else {
    LOG_ERR("CAT", "Download failed: %d", static_cast<int>(result));
    fail(StrId::STR_DOWNLOAD_FAILED);
  }
}
