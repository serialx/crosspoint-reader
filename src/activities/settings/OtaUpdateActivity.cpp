#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/OtaUpdater.h"

void OtaUpdateActivity::onWifiSelectionComplete(bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    const auto action = serviceResult(XP_UI_UPDATE_WIFI_RESULT, 0);
    performAction(static_cast<XpUiUpdateAction>(action), 0);
    return;
  }
  LOG_DBG("OTA", "WiFi connected, checking for update");
  notify(XP_UI_UPDATE_WIFI_RESULT, 1);
  requestUpdateAndWait();
  const auto res = updater.checkForUpdate();
  if (res == OtaUpdater::NO_UPDATE) {
    LOG_DBG("OTA", "No firmware asset for this board in latest release");
    notify(XP_UI_UPDATE_OTA_CHECKED, res, 0);
    return;
  }
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    notify(XP_UI_UPDATE_OTA_CHECKED, res, 0);
    return;
  }
  if (!updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    notify(XP_UI_UPDATE_OTA_CHECKED, res, 0);
    return;
  }
  notify(XP_UI_UPDATE_OTA_CHECKED, res, 1);
  requestUpdate();
}
void OtaUpdateActivity::onEnter() {
  UpdateUiActivity::onEnter();

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}
void OtaUpdateActivity::onExit() {
  UpdateUiActivity::onExit();

  // Successful installs use a normal restart. Leaving with Wi-Fi active uses
  // a silent restart to release the fragmented LWIP/mbedTLS heap.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}
void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  notify(XP_UI_UPDATE_INSTALL_BEGIN);
  notify(XP_UI_UPDATE_PROGRESS, updater.getProcessedSize(), updater.getTotalSize());
  requestUpdateAndWait();
  const auto res = updater.installUpdate(
      [](void* p) {
        auto& self = *static_cast<OtaUpdateActivity*>(p);
        self.notify(XP_UI_UPDATE_PROGRESS, self.updater.getProcessedSize(), self.updater.getTotalSize());
        self.requestUpdate(true);
      },
      this);
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    failedDetail = res == OtaUpdater::WRONG_DEVICE_ERROR ? tr(STR_FIRMWARE_WRONG_DEVICE) : nullptr;
    notify(XP_UI_UPDATE_INSTALLED, res);
    requestUpdate();
    return;
  }
  notify(XP_UI_UPDATE_INSTALLED, 0);
  requestUpdateAndWait();
  delay(3000);
  notify(XP_UI_UPDATE_SHUTDOWN);
}
void OtaUpdateActivity::performAction(XpUiUpdateAction action, int32_t) {
  switch (action) {
    case XP_UI_UPDATE_FINISH:
      finish();
      break;
    case XP_UI_UPDATE_INSTALL:
      runUpdateInstall();
      break;
    case XP_UI_UPDATE_RESTART:
      ESP.restart();
      break;
    default:
      break;
  }
}
void OtaUpdateActivity::prepareText() {
  if (painting.phase == 2 && currentVersion.empty()) {
    currentVersion = std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION;
    newVersion = std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion();
  }
  snprintf(bytes, sizeof(bytes), "%u / %u", unsigned(painting.processed), unsigned(painting.total));
}
const char* OtaUpdateActivity::dynamicText(uint32_t handle) const {
  switch (handle) {
    case 0x40000013:
      return failedDetail ? failedDetail : "";
    case 0x40000014:
      return currentVersion.c_str();
    case 0x40000015:
      return newVersion.c_str();
    case 0x40000016:
      return bytes;
    default:
      return "";
  }
}
void OtaUpdateActivity::afterHeader() const {
  if (painting.phase == 3) LOG_DBG("OTA", "Update progress: %d / %d", painting.processed, painting.total);
}
