#include "WifiSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "NetworkUiBinding.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/PluginEvents.h"
namespace fui = freeink::ui;
WifiSelectionActivity::WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& input, bool automatic)
    : RustActivityAdapter("WifiSelection", renderer, input),
      textTarget(makeUiTarget(renderer)),
      networks{},
      networkStatuses{},
      autoAttemptedSsids{},
      selectedSSID{},
      connectedIP{},
      connectionError{},
      enteredPassword{},
      cachedMacAddress{},
      promptLabel{},
      paintingSsid{},
      paintingIp{},
      allowAutoConnect(automatic) {}
WifiSelectionActivity::~WifiSelectionActivity() {
  visibleRows.reset();
  releaseResources();
}
void WifiSelectionActivity::releaseResources() {
  if (!resourcesLive) return;
  resourcesLive = false;
  paintingIp.~optional();
  paintingSsid.~optional();
  promptLabel.~optional();
  cachedMacAddress.~basic_string();
  enteredPassword.~basic_string();
  connectionError.~basic_string();
  connectedIP.~basic_string();
  selectedSSID.~basic_string();
  autoAttemptedSsids.~vector();
  networkStatuses.~vector();
  networks.~vector();
  textTarget.~GfxRendererTarget();
}
void WifiSelectionActivity::onEnter() { Activity::onEnter(); }
void WifiSelectionActivity::onExit() { Activity::onExit(); }
bool WifiSelectionActivity::CallContext::inputCurrent() {
  // A committed action must finish its model publication and lifecycle effects.
  if (!inputAction || committed) return true;
  if (activity.rustSceneRejected()) inputRejected = true;
  ModelLock lock(activity);
  inputRejected |= activity.model.list.navigation_revision != navigationRevision;
  return !inputRejected;
}
void WifiSelectionActivity::run(XpUiWifiEvent event, uint32_t value, const XpUiInput* input) {
  XpUiWifiState working;
  {
    ModelLock lock(*this);
    working = model;
  }
  CallContext context{*this, {}, {}};
  context.navigationRevision = working.list.navigation_revision;
  context.inputAction = input && (((input->pressed | input->released | input->held) & ~XP_UI_BACK) ||
                                  input->touch_flags || input->swipe || input->action);
  if (!context.inputCurrent()) return;
  const XpUiWifiServices services{&context, service, publish};
  xp_ui_network_wifi_run(&working, &services, event, value, input);
}
void WifiSelectionActivity::publish(void* ctx, XpUiWifiState* next) {
  auto& call = *static_cast<CallContext*>(ctx);
  auto& self = call.activity;
  if (!call.inputCurrent()) return;
  const bool rejected = self.rustSceneRejected();
  bool leaving = false;
  {
    ModelLock lock(self);
    if (!call.committed && call.inputAction &&
        (rejected || self.model.list.navigation_revision != call.navigationRevision)) {
      call.inputRejected = true;
      return;
    }
    leaving = self.model.phase == XP_UI_WIFI_PHASE_LIST && next->phase != XP_UI_WIFI_PHASE_LIST;
    if (rejected || self.model.list.navigation_revision != call.navigationRevision) {
      next->list.active = -1;
      next->list.last_repeat = 0;
      next->list.pending_scroll = 0;
      next->list.navigation_revision = self.model.list.navigation_revision + 1;
    }
    xp_ui_network_wifi_publish(&self.model, next);
    call.committed = true;
    call.navigationRevision = self.model.list.navigation_revision;
  }
  if (leaving) self.retireRows();
}
void WifiSelectionActivity::retireRows() {
  ResourceLock lock(*this);
  retireRequested = true;
  if (!paintingRows) {
    visibleRows.reset();
    rowCapacity = 0;
  }
}
uint32_t WifiSelectionActivity::service(void* ctx, uint32_t operation, uint32_t value) {
  auto& call = *static_cast<CallContext*>(ctx);
  auto& self = call.activity;
  if (!call.inputCurrent()) return 0;
  switch (static_cast<XpUiWifiCall>(operation)) {
    case XP_UI_WIFI_CALL_NOW:
      return millis();
    case XP_UI_WIFI_CALL_PAINT:
      self.requestUpdate();
      break;
    case XP_UI_WIFI_CALL_ENTRY_RESET: {
      {
        RenderLock lock(self);
        if (!call.inputCurrent()) return 0;
        WIFI_STORE.loadFromFile();
      }
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.networks.clear();
      self.networkStatuses.clear();
      self.selectedSSID.clear();
      self.connectedIP.clear();
      self.connectionError.clear();
      self.enteredPassword.clear();
      self.autoAttemptedSsids.clear();
      const auto count = WIFI_STORE.getCredentialCount();
      self.autoAttemptedSsids.reserve(count);
      return count;
    }
    case XP_UI_WIFI_CALL_READ_MAC: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      uint8_t mac[6] = {};
      char macStr[64];
      const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
      if (macResult == ESP_OK) {
        snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0], mac[1],
                 mac[2], mac[3], mac[4], mac[5]);
      } else {
        LOG_ERR("WIFI", "Failed to read station MAC (err=%d)", static_cast<int>(macResult));
        snprintf(macStr, sizeof(macStr), "%s --", tr(STR_MAC_ADDRESS));
      }
      self.cachedMacAddress = std::string(macStr);

      break;
    }
    case XP_UI_WIFI_CALL_EXIT: {
      LOG_DBG("WIFI", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());

      // Stop any ongoing WiFi scan
      LOG_DBG("WIFI", "Deleting WiFi scan...");
      WiFi.scanDelete();
      LOG_DBG("WIFI", "Free heap after scanDelete: %d bytes", ESP.getFreeHeap());

      // Note: We do NOT disconnect WiFi here - the parent activity
      // (CrossPointWebServerActivity) manages WiFi connection state. We just clean
      // up the scan and task.

      LOG_DBG("WIFI", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());

      break;
    }
    case XP_UI_WIFI_CALL_LAST_CREDENTIAL: {
      call.lastSsid.emplace(WIFI_STORE.getLastConnectedSsid());
      if (!call.lastSsid->empty()) call.credential = WIFI_STORE.findCredential(*call.lastSsid);
      return call.credential.has_value();
    }
    case XP_UI_WIFI_CALL_CREDENTIAL_FLAGS: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      if (!call.credential) return 0;
      const auto& cred = *call.credential;
      return (std::find(self.autoAttemptedSsids.begin(), self.autoAttemptedSsids.end(), cred.ssid) !=
                      self.autoAttemptedSsids.end()
                  ? 1
                  : 0) |
             (!cred.password.empty() ? 2 : 0);
    }
    case XP_UI_WIFI_CALL_COPY_AUTO_CREDENTIAL: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      const auto& cred = *call.credential;
      LOG_DBG("WIFI", "Attempting saved network: %s", cred.ssid.c_str());
      self.autoAttemptedSsids.push_back(cred.ssid);
      call.committed = true;
      self.selectedSSID = cred.ssid;
      self.enteredPassword = cred.password;
      break;
    }
    case XP_UI_WIFI_CALL_DROP_CREDENTIAL:
      call.credential.reset();
      break;
    case XP_UI_WIFI_CALL_CLEAR_SCAN: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.networks.clear();
      self.networkStatuses.clear();
      break;
    }
    case XP_UI_WIFI_CALL_WIFI_STA:
      WiFi.mode(WIFI_STA);
      break;
    case XP_UI_WIFI_CALL_DISCONNECT:
      WiFi.disconnect();
      call.committed = true;
      break;
    case XP_UI_WIFI_CALL_DELAY:
      delay(value);
      break;
    case XP_UI_WIFI_CALL_SCAN_BEGIN:
      WiFi.scanNetworks(true);
      break;
    case XP_UI_WIFI_CALL_SCAN_COMPLETE:
      return static_cast<uint32_t>(WiFi.scanComplete());
    case XP_UI_WIFI_CALL_SCAN_RESERVE: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.networks.clear();
      self.networks.reserve(value);
      call.committed = true;
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_READ:
      strlcpy(call.scanSsid, WiFi.SSID(value).c_str(), sizeof(call.scanSsid));
      call.scanRssi = WiFi.RSSI(value);
      call.scanIndex = value;
      return call.scanSsid[0] != 0;
    case XP_UI_WIFI_CALL_SCAN_FIND: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      auto found = std::find_if(self.networks.begin(), self.networks.end(),
                                [&call](const WifiNetworkInfo& n) { return n.ssid == call.scanSsid; });
      return found == self.networks.end() ? UINT32_MAX : static_cast<uint32_t>(found - self.networks.begin());
    }
    case XP_UI_WIFI_CALL_SCAN_APPEND: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      WifiNetworkInfo network;
      network.ssid = call.scanSsid;
      network.rssi = call.scanRssi;
      network.isEncrypted = WiFi.encryptionType(value) != WIFI_AUTH_OPEN;
      network.hasSavedPassword = WIFI_STORE.hasSavedCredential(network.ssid);
      self.networks.push_back(std::move(network));
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_RSSI: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      return static_cast<uint32_t>(value == UINT32_MAX ? call.scanRssi : self.networks[value].rssi);
    }
    case XP_UI_WIFI_CALL_SCAN_REPLACE: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      auto& n = self.networks[value];
      n.rssi = call.scanRssi;
      n.isEncrypted = WiFi.encryptionType(call.scanIndex) != WIFI_AUTH_OPEN;
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_SORT: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      std::sort(self.networks.begin(), self.networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
        return xp_ui_network_wifi_row_before(a.hasSavedPassword, a.rssi, b.hasSavedPassword, b.rssi) != 0;
      });
      break;
    }
    case XP_UI_WIFI_CALL_ROW_COUNT: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      return self.networks.size();
    }
    case XP_UI_WIFI_CALL_HIDDEN_APPEND: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.appendHiddenNetworkEntry();
      break;
    }
    case XP_UI_WIFI_CALL_ROWS_REBUILD: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.rebuildNetworkStatuses();
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_DELETE:
      WiFi.scanDelete();
      call.committed = true;
      break;
    case XP_UI_WIFI_CALL_ROW_FLAGS: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      if (value >= self.networks.size()) return 0;
      const auto& n = self.networks[value];
      return (n.hasSavedPassword ? 1 : 0) | (n.isHiddenPlaceholder ? 2 : 0) | (n.isEncrypted ? 4 : 0);
    }
    case XP_UI_WIFI_CALL_ROW_ATTEMPTED: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      return std::find(self.autoAttemptedSsids.begin(), self.autoAttemptedSsids.end(), self.networks[value].ssid) !=
             self.autoAttemptedSsids.end();
    }
    case XP_UI_WIFI_CALL_ROW_CREDENTIAL: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      call.credential = WIFI_STORE.findCredential(self.networks[value].ssid);
      return call.credential.has_value();
    }
    case XP_UI_WIFI_CALL_SELECT_ROW: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.selectedSSID = self.networks[value].ssid;
      call.committed = true;
      self.enteredPassword.clear();
      break;
    }
    case XP_UI_WIFI_CALL_SELECTED_CREDENTIAL: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      call.credential = WIFI_STORE.findCredential(self.selectedSSID);
      return call.credential.has_value();
    }
    case XP_UI_WIFI_CALL_COPY_PASSWORD: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.enteredPassword = call.credential->password;
      call.committed = true;
      break;
    }
    case XP_UI_WIFI_CALL_PASSWORD_PRESENT: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      return !self.enteredPassword.empty();
    }
    case XP_UI_WIFI_CALL_LOG_SAVED: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      if (value)
        LOG_DBG("WiFi", "Using saved password for hidden network %s", self.selectedSSID.c_str());
      else
        LOG_DBG("WiFi", "Using saved password for %s, length: %zu", self.selectedSSID.c_str(),
                self.enteredPassword.size());
      break;
    }
    case XP_UI_WIFI_CALL_HIDDEN_RESET: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.selectedSSID.clear();
      call.committed = true;
      self.enteredPassword.clear();
      break;
    }
    case XP_UI_WIFI_CALL_KEYBOARD:
      self.keyboard(value != 0);
      break;
    case XP_UI_WIFI_CALL_CONNECTION_RESET: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.connectedIP.clear();
      call.committed = true;
      self.connectionError.clear();
      break;
    }
    case XP_UI_WIFI_CALL_CONNECTION_START: {
      WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect
      WiFi.mode(WIFI_STA);
      WiFi.disconnect(true, true);  // Abort any in-progress SDK auto-connect and clear NVS-saved SSID
      delay(100);

      // Scan all channels so self.networks with multiple APs use the strongest matching
      // BSSID instead of the first match found by the framework's default fast scan.
      WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
      WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

      // Set hostname so routers show "CrossPoint-Reader-AABBCCDDEEFF" instead of "esp32-XXXXXXXXXXXX"
      uint8_t mac[6] = {};
      const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
      if (macResult == ESP_OK) {
        char hostname[sizeof("CrossPoint-Reader-") + 12];
        snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
        WiFi.setHostname(hostname);
      } else {
        LOG_ERR("WIFI", "Failed to read station MAC for hostname (err=%d)", static_cast<int>(macResult));
      }

      if (value != 0) {
        WiFi.begin(self.selectedSSID.c_str(), self.enteredPassword.c_str());
      } else {
        WiFi.begin(self.selectedSSID.c_str());
      }

      break;
    }
    case XP_UI_WIFI_CALL_CONNECTION_STATUS:
      return WiFi.status();
    case XP_UI_WIFI_CALL_CONNECTED_DETAILS: {
      IPAddress ip = WiFi.localIP();
      char ipStr[16];
      snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      {
        ResourceLock lock(self);
        if (!call.inputCurrent()) return 0;
        self.connectedIP = ipStr;
        call.committed = true;
      }

#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
      uint8_t connectedBssid[6] = {};
      WiFi.BSSID(connectedBssid);
      LOG_DBG("WIFI", "Connected BSSID: %02x:%02x:%02x:%02x:%02x:%02x, channel: %d, RSSI: %d dBm",
              static_cast<unsigned>(connectedBssid[0]), static_cast<unsigned>(connectedBssid[1]),
              static_cast<unsigned>(connectedBssid[2]), static_cast<unsigned>(connectedBssid[3]),
              static_cast<unsigned>(connectedBssid[4]), static_cast<unsigned>(connectedBssid[5]), WiFi.channel(),
              WiFi.RSSI());
#endif

      // Sync RTC from NTP on the first successful WiFi connection only. The DS3231
      // drifts ~2 ppm so one sync is enough; users can force a re-sync from
      // Settings > System > Clock > Sync clock now.
      if (halClock.isAvailable() && !SETTINGS.clockHasBeenSynced) {
        if (halClock.syncFromNTP()) {
          SETTINGS.clockHasBeenSynced = 1;
          SETTINGS.saveToFile();
        }
      }

      // Every station join is a chance to snap the loan-clock floor to real
      // time (non-blocking; see TrustedTime).
      trustedtime::startSync();

      // Save this as the last connected network - SD card operations need lock as
      // we use SPI for both
      {
        RenderLock lock(self);
        if (!call.inputCurrent()) return 0;
        WIFI_STORE.setLastConnectedSsid(self.selectedSSID);
      }

      // Every station join is a window where plugin senders are deliverable, so
      // drain the plugin outboxes here (web server up and sleep entry are the
      // other such moments). Cheap no-op when nothing is queued.
      pluginevents::drain(&self.renderer);

      break;
    }
    case XP_UI_WIFI_CALL_SET_ERROR: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.connectionError = value == 2 ? tr(STR_ERROR_CONNECTION_TIMEOUT) : tr(STR_ERROR_GENERAL_FAILURE);
      call.committed = true;
      if (value == 1) self.connectionError = tr(STR_ERROR_NETWORK_NOT_FOUND);
      break;
    }
    case XP_UI_WIFI_CALL_LOG_COMPLETE:
      LOG_DBG("WIFI", "Connected with saved/open credentials, completing immediately");
      break;
    case XP_UI_WIFI_CALL_COMPLETE:
      self.onComplete(value != 0);
      break;
    case XP_UI_WIFI_CALL_LOG_AUTO_FAILURE: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      LOG_DBG("WIFI", "Saved network failed: %s", self.selectedSSID.c_str());
      break;
    }
    case XP_UI_WIFI_CALL_LOG_MANUAL:
      LOG_DBG("WIFI", "User requested manual network list");
      break;
    case XP_UI_WIFI_CALL_SAVE: {
      RenderLock renderLock(self);
      if (!call.inputCurrent()) return 0;
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      WIFI_STORE.addCredential(self.selectedSSID, self.enteredPassword);
      call.committed = true;
      break;
    }
    case XP_UI_WIFI_CALL_FORGET: {
      RenderLock renderLock(self);
      if (!call.inputCurrent()) return 0;
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      WIFI_STORE.removeCredential(self.selectedSSID);
      call.committed = true;
      auto n = std::find_if(self.networks.begin(), self.networks.end(),
                            [&self](const WifiNetworkInfo& n) { return n.ssid == self.selectedSSID; });
      if (n != self.networks.end()) n->hasSavedPassword = false;
      break;
    }
    case XP_UI_WIFI_CALL_HAPTIC:
      haptic_feedback::touchAction(value != 0);
      break;
    case XP_UI_WIFI_CALL_COPY_SELECTED: {
      ResourceLock lock(self);
      if (!call.inputCurrent()) return 0;
      self.selectedSSID = self.networks[value].ssid;
      call.committed = true;
      break;
    }
  }
  return 0;
}
void WifiSelectionActivity::keyboard(bool hidden) {
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                             renderer, mappedInput, hidden ? tr(STR_ENTER_WIFI_SSID) : tr(STR_ENTER_WIFI_PASSWORD), "",
                             hidden ? 32 : 64, InputType::Text),
                         [this, hidden](const ActivityResult& result) {
                           bool empty = false;
                           if (!result.isCancelled) {
                             ResourceLock lock(*this);
                             auto& target = hidden ? selectedSSID : enteredPassword;
                             target = std::get<KeyboardResult>(result.data).text;
                             empty = target.empty();
                           }
                           run(XP_UI_WIFI_EVENT_KEYBOARD, (result.isCancelled ? 1 : 0) | (empty ? 2 : 0));
                         });
}
void WifiSelectionActivity::appendHiddenNetworkEntry() {
  // Synthetic list entry that lets the user type an SSID that is not broadcast.
  // ESP32 can join hidden APs as long as the SSID is supplied to WiFi.begin().
  WifiNetworkInfo placeholder;
  placeholder.rssi = 0;
  placeholder.isEncrypted = true;  // Treated as encrypted; an empty password still connects open APs
  placeholder.hasSavedPassword = false;
  placeholder.isHiddenPlaceholder = true;
  networks.push_back(std::move(placeholder));
}

void WifiSelectionActivity::rebuildNetworkStatuses() {
  networkStatuses.assign(networks.size(), std::string());
  for (size_t i = 0; i < networks.size(); i++) {
    const auto& network = networks[i];
    if (!network.isHiddenPlaceholder) {
      networkStatuses[i] = std::string(network.hasSavedPassword ? "+ " : "") + (network.isEncrypted ? "* " : "") +
                           getSignalStrengthIndicator(network.rssi);
    }
  }
}

std::string WifiSelectionActivity::getSignalStrengthIndicator(int32_t rssi) {
  static constexpr const char* bars[] = {"||||", " |||", "  ||", "   |"};
  return bars[xp_ui_network_wifi_signal_strength(rssi)];
}

void WifiSelectionActivity::onComplete(const bool connected) {
  ActivityResult result;
  result.isCancelled = !connected;
  if (connected) {
    result.data = WifiResult{true, selectedSSID, connectedIP};
  }
  setResult(std::move(result));
  finish();
}
void WifiSelectionActivity::loop() {}
void WifiSelectionActivity::rustSceneFailed() {}
void WifiSelectionActivity::rustPublished() {}
const char* WifiSelectionActivity::text(uint32_t handle) const {
  if (handle > 0 && handle < XP_UI_WIFI_HEADER) {
    const auto index = (handle - 1) / 4;
    if (!rowCapacity) return nullptr;
    const auto& row = visibleRows[index % rowCapacity];
    if (row.index != index) return nullptr;
    switch ((handle - 1) % 4) {
      case 0:
        return row.hidden ? tr(STR_ADD_HIDDEN_NETWORK) : row.ssid;
      case 2:
        return row.status[0] ? row.status : nullptr;
      default:
        return nullptr;
    }
  }
  switch (handle) {
    case XP_UI_WIFI_EMPTY:
      return tr(STR_NO_NETWORKS);
    case XP_UI_WIFI_RETRY:
      return tr(STR_RETRY);
    case XP_UI_WIFI_PRESS_SCAN:
      return tr(STR_PRESS_OK_SCAN);
    case XP_UI_WIFI_SCANNING:
      return tr(STR_SCANNING);
    case XP_UI_WIFI_FINDING_SAVED:
      return tr(STR_FINDING_SAVED_WIFI);
    case XP_UI_WIFI_CONNECTING:
      return tr(STR_CONNECTING);
    case XP_UI_WIFI_CONNECTING_SAVED:
      return tr(STR_CONNECTING_SAVED_WIFI);
    case XP_UI_WIFI_PROMPT_CONNECTED:
    case XP_UI_WIFI_CONNECTED:
      return tr(STR_CONNECTED);
    case XP_UI_WIFI_FAILED:
      return tr(STR_CONNECTION_FAILED);
    case XP_UI_WIFI_ERROR:
      return connectionError.c_str();
    case XP_UI_WIFI_PROMPT_FORGET:
    case XP_UI_WIFI_FORGET:
      return tr(STR_FORGET_NETWORK);
    case XP_UI_WIFI_PROMPT_SSID:
      return promptLabel ? promptLabel->c_str() : nullptr;
    case XP_UI_WIFI_REMOVE:
      return tr(STR_FORGET_AND_REMOVE);
    case XP_UI_WIFI_SAVE_PASSWORD:
      return tr(STR_SAVE_PASSWORD);
    case XP_UI_WIFI_CANCEL:
      return tr(STR_CANCEL);
    case XP_UI_WIFI_YES:
      return tr(STR_YES);
    case XP_UI_WIFI_NO:
      return tr(STR_NO);
    default:
      return nullptr;
  }
}
XpUiWifiViewServices WifiSelectionActivity::resources() {
  return {{this,
           [](void* context, uint16_t index, XpUiSettingsRow* out) -> uint8_t {
             auto& self = *static_cast<WifiSelectionActivity*>(context);
             if (index >= self.networks.size()) return 0;
             if (!self.rowCapacity) return 0;
             auto& row = self.visibleRows[index % self.rowCapacity];
             if (row.index != index) {
               const auto& network = self.networks[index];
               strlcpy(row.ssid, network.ssid.c_str(), sizeof(row.ssid));
               strlcpy(row.status, self.networkStatuses[index].c_str(), sizeof(row.status));
               row.hidden = network.isHiddenPlaceholder;
               row.index = index;
             }
             *out = {row.hidden ? tr(STR_ADD_HIDDEN_NETWORK) : row.ssid,
                     nullptr,
                     row.status[0] ? row.status : nullptr,
                     0,
                     0,
                     0,
                     1,
                     0};
             return 1;
           },
           [](void* context, const char* text, int32_t font, uint8_t bold) -> int32_t {
             return static_cast<WifiSelectionActivity*>(context)->renderer.getTextWidth(
                 font, text, static_cast<EpdFontFamily::Style>(bold));
           },
           [](void* context, const char* text, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
             auto& self = *static_cast<WifiSelectionActivity*>(context);
             self.textTarget.setFont(3, font);
             fui::TextStyle style;
             style.font = 3;
             style.bold = bold;
             style.maxLines = lines;
             return fui::measureWrappedText(self.textTarget, text, style, width).height;
           },
           [](void* context, int32_t font) -> int32_t {
             return static_cast<WifiSelectionActivity*>(context)->renderer.getLineHeight(font);
           },
           renderingState.count, 0},
          [](void* context, uint32_t handle) -> const char* {
            return static_cast<WifiSelectionActivity*>(context)->text(handle);
          }};
}
bool WifiSelectionActivity::rustSceneCurrent() const { return false; }
bool WifiSelectionActivity::buildRustScene(XpUiScene&) { return false; }

void WifiSelectionActivity::render(RenderLock&&) {}
XpUiPaint WifiSelectionActivity::rustResources() { return {}; }
