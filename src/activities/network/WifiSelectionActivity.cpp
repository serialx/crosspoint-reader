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
      allowAutoConnect(automatic) {}
void WifiSelectionActivity::onEnter() {
  Activity::onEnter();
  {
    ModelLock lock(*this);
    model.allow_auto = allowAutoConnect;
    routingReady = false;
  }
  run(XP_UI_WIFI_EVENT_ENTER);
}
void WifiSelectionActivity::onExit() {
  Activity::onExit();
  run(XP_UI_WIFI_EVENT_EXIT);
  retireRows();
}
void WifiSelectionActivity::run(XpUiWifiEvent event, uint32_t value, const XpUiInput* input) {
  XpUiWifiState working;
  {
    ModelLock lock(*this);
    working = model;
  }
  CallContext context{*this, {}, {}};
  context.navigationRevision = working.list.navigation_revision;
  const XpUiWifiServices services{&context, service, publish};
  xp_ui_network_wifi_run(&working, &services, event, value, input);
}
void WifiSelectionActivity::publish(void* ctx, XpUiWifiState* next) {
  auto& call = *static_cast<CallContext*>(ctx);
  auto& self = call.activity;
  const bool rejected = self.rustSceneRejected();
  bool leaving = false;
  {
    ModelLock lock(self);
    leaving = self.model.phase == XP_UI_WIFI_PHASE_LIST && next->phase != XP_UI_WIFI_PHASE_LIST;
    if (rejected || self.model.list.navigation_revision != call.navigationRevision) {
      next->list.active = -1;
      next->list.last_repeat = 0;
      next->list.pending_scroll = 0;
      next->list.navigation_revision = self.model.list.navigation_revision + 1;
    }
    xp_ui_network_wifi_publish(&self.model, next);
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
  switch (static_cast<XpUiWifiCall>(operation)) {
    case XP_UI_WIFI_CALL_NOW:
      return millis();
    case XP_UI_WIFI_CALL_PAINT:
      self.requestUpdate();
      break;
    case XP_UI_WIFI_CALL_ENTRY_RESET: {
      {
        RenderLock lock(self);
        WIFI_STORE.loadFromFile();
      }
      ResourceLock lock(self);
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
      const auto& cred = *call.credential;
      LOG_DBG("WIFI", "Attempting saved network: %s", cred.ssid.c_str());
      self.autoAttemptedSsids.push_back(cred.ssid);
      self.selectedSSID = cred.ssid;
      self.enteredPassword = cred.password;
      break;
    }
    case XP_UI_WIFI_CALL_DROP_CREDENTIAL:
      call.credential.reset();
      break;
    case XP_UI_WIFI_CALL_CLEAR_SCAN: {
      ResourceLock lock(self);
      self.networks.clear();
      self.networkStatuses.clear();
      break;
    }
    case XP_UI_WIFI_CALL_WIFI_STA:
      WiFi.mode(WIFI_STA);
      break;
    case XP_UI_WIFI_CALL_DISCONNECT:
      WiFi.disconnect();
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
      self.networks.clear();
      self.networks.reserve(value);
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_READ:
      strlcpy(call.scanSsid, WiFi.SSID(value).c_str(), sizeof(call.scanSsid));
      call.scanRssi = WiFi.RSSI(value);
      call.scanIndex = value;
      return call.scanSsid[0] != 0;
    case XP_UI_WIFI_CALL_SCAN_FIND: {
      ResourceLock lock(self);
      auto found = std::find_if(self.networks.begin(), self.networks.end(),
                                [&call](const WifiNetworkInfo& n) { return n.ssid == call.scanSsid; });
      return found == self.networks.end() ? UINT32_MAX : static_cast<uint32_t>(found - self.networks.begin());
    }
    case XP_UI_WIFI_CALL_SCAN_APPEND: {
      ResourceLock lock(self);
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
      return static_cast<uint32_t>(value == UINT32_MAX ? call.scanRssi : self.networks[value].rssi);
    }
    case XP_UI_WIFI_CALL_SCAN_REPLACE: {
      ResourceLock lock(self);
      auto& n = self.networks[value];
      n.rssi = call.scanRssi;
      n.isEncrypted = WiFi.encryptionType(call.scanIndex) != WIFI_AUTH_OPEN;
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_SORT: {
      ResourceLock lock(self);
      std::sort(self.networks.begin(), self.networks.end(), [](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
        return xp_ui_network_wifi_row_before(a.hasSavedPassword, a.rssi, b.hasSavedPassword, b.rssi) != 0;
      });
      break;
    }
    case XP_UI_WIFI_CALL_ROW_COUNT: {
      ResourceLock lock(self);
      return self.networks.size();
    }
    case XP_UI_WIFI_CALL_HIDDEN_APPEND: {
      ResourceLock lock(self);
      self.appendHiddenNetworkEntry();
      break;
    }
    case XP_UI_WIFI_CALL_ROWS_REBUILD: {
      ResourceLock lock(self);
      self.rebuildNetworkStatuses();
      break;
    }
    case XP_UI_WIFI_CALL_SCAN_DELETE:
      WiFi.scanDelete();
      break;
    case XP_UI_WIFI_CALL_ROW_FLAGS: {
      ResourceLock lock(self);
      if (value >= self.networks.size()) return 0;
      const auto& n = self.networks[value];
      return (n.hasSavedPassword ? 1 : 0) | (n.isHiddenPlaceholder ? 2 : 0) | (n.isEncrypted ? 4 : 0);
    }
    case XP_UI_WIFI_CALL_ROW_ATTEMPTED: {
      ResourceLock lock(self);
      return std::find(self.autoAttemptedSsids.begin(), self.autoAttemptedSsids.end(), self.networks[value].ssid) !=
             self.autoAttemptedSsids.end();
    }
    case XP_UI_WIFI_CALL_ROW_CREDENTIAL: {
      ResourceLock lock(self);
      call.credential = WIFI_STORE.findCredential(self.networks[value].ssid);
      return call.credential.has_value();
    }
    case XP_UI_WIFI_CALL_SELECT_ROW: {
      ResourceLock lock(self);
      self.selectedSSID = self.networks[value].ssid;
      self.enteredPassword.clear();
      break;
    }
    case XP_UI_WIFI_CALL_SELECTED_CREDENTIAL: {
      ResourceLock lock(self);
      call.credential = WIFI_STORE.findCredential(self.selectedSSID);
      return call.credential.has_value();
    }
    case XP_UI_WIFI_CALL_COPY_PASSWORD: {
      ResourceLock lock(self);
      self.enteredPassword = call.credential->password;
      break;
    }
    case XP_UI_WIFI_CALL_PASSWORD_PRESENT: {
      ResourceLock lock(self);
      return !self.enteredPassword.empty();
    }
    case XP_UI_WIFI_CALL_LOG_SAVED: {
      ResourceLock lock(self);
      if (value)
        LOG_DBG("WiFi", "Using saved password for hidden network %s", self.selectedSSID.c_str());
      else
        LOG_DBG("WiFi", "Using saved password for %s, length: %zu", self.selectedSSID.c_str(),
                self.enteredPassword.size());
      break;
    }
    case XP_UI_WIFI_CALL_HIDDEN_RESET: {
      ResourceLock lock(self);
      self.selectedSSID.clear();
      self.enteredPassword.clear();
      break;
    }
    case XP_UI_WIFI_CALL_KEYBOARD:
      self.keyboard(value != 0);
      break;
    case XP_UI_WIFI_CALL_CONNECTION_RESET: {
      ResourceLock lock(self);
      self.connectedIP.clear();
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
        self.connectedIP = ipStr;
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
      self.connectionError = value == 2 ? tr(STR_ERROR_CONNECTION_TIMEOUT) : tr(STR_ERROR_GENERAL_FAILURE);
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
      LOG_DBG("WIFI", "Saved network failed: %s", self.selectedSSID.c_str());
      break;
    }
    case XP_UI_WIFI_CALL_LOG_MANUAL:
      LOG_DBG("WIFI", "User requested manual network list");
      break;
    case XP_UI_WIFI_CALL_SAVE: {
      RenderLock renderLock(self);
      ResourceLock lock(self);
      WIFI_STORE.addCredential(self.selectedSSID, self.enteredPassword);
      break;
    }
    case XP_UI_WIFI_CALL_FORGET: {
      RenderLock renderLock(self);
      ResourceLock lock(self);
      WIFI_STORE.removeCredential(self.selectedSSID);
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
      self.selectedSSID = self.networks[value].ssid;
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
void WifiSelectionActivity::loop() {
  const bool blocked = consumeRejectedInput();
  XpUiInput input{};
  constexpr MappedInputManager::Button buttons[] = {
      MappedInputManager::Button::Back,        MappedInputManager::Button::Confirm, MappedInputManager::Button::NavNext,
      MappedInputManager::Button::NavPrevious, MappedInputManager::Button::Left,    MappedInputManager::Button::Right,
      MappedInputManager::Button::Up,          MappedInputManager::Button::Down};
  for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    if (mappedInput.wasPressed(buttons[i])) input.pressed |= 1u << i;
    if (mappedInput.wasReleased(buttons[i])) input.released |= 1u << i;
    if (mappedInput.isPressed(buttons[i])) input.held |= 1u << i;
  }
  input.now_ms = millis();
  input.held_ms = mappedInput.getHeldTime();
  uint8_t phase;
  {
    ModelLock lock(*this);
    phase = model.phase;
  }
  const auto touch = touchSnapshotFrom(mappedInput, phase == XP_UI_WIFI_PHASE_LIST);
  input.touch_x = touch.touchX;
  input.touch_y = touch.touchY;
  input.touch_flags = (touch.touchPressed ? XP_UI_TOUCH_PRESSED : 0) |
                      (touch.touchReleased ? XP_UI_TOUCH_RELEASED : 0) | (touch.touchHeld ? XP_UI_TOUCH_HELD : 0);
  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      input.swipe = XP_UI_SWIPE_UP;
      break;
    case MappedInputManager::SwipeDir::Down:
      input.swipe = XP_UI_SWIPE_DOWN;
      break;
    case MappedInputManager::SwipeDir::Left:
      input.swipe = XP_UI_SWIPE_LEFT;
      break;
    case MappedInputManager::SwipeDir::Right:
      input.swipe = XP_UI_SWIPE_RIGHT;
      break;
    case MappedInputManager::SwipeDir::None:
      break;
  }
  bool routed = false;
  {
    ModelLock lock(*this);
    if (blocked) {
      input.pressed &= XP_UI_BACK;
      input.released &= XP_UI_BACK;
      input.held = 0;
      input.swipe = 0;
      input.touch_flags = 0;
    }
    if (routingReady && input.touch_flags) {
      routed = (input.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_RELEASED)) != 0;
      xp_ui_network_wifi_hit(&hits, &model, &input);
    }
  }
  run(XP_UI_WIFI_EVENT_TICK, (!blocked && touch.longPress ? 1 : 0) | (routed ? 2 : 0), &input);
}
void WifiSelectionActivity::rustSceneFailed() {
  routingReady = false;
  model.list.active = -1;
  model.list.last_repeat = 0;
  model.list.pending_scroll = 0;
  model.list.navigation_revision++;
  model.flash_ticks = 0;
  model.flash_index = -1;
}
void WifiSelectionActivity::rustPublished() {
  xp_ui_network_wifi_view_publish(&model, &renderingState, &next);
  if (model.generation == renderingState.generation && model.phase == renderingState.phase &&
      model.count == renderingState.count) {
    hits = nextHits;
    routingReady = true;
  }
}
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
bool WifiSelectionActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    xp_ui_network_wifi_prepare_view(&model);
    renderingState = model;
  }
  if (renderingState.phase == XP_UI_WIFI_PHASE_LIST &&
      (renderingState.count != networks.size() || networkStatuses.size() != networks.size()))
    return false;
  strlcpy(renderingMac, cachedMacAddress.c_str(), sizeof(renderingMac));
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = textTarget.deviceContext();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  XpUiWifiLayout layout{};
  auto& b = layout.base;
  b.list.screen = {0, 0, device.width, device.height};
  b.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
            static_cast<int16_t>(safe.height)};
  const auto deviceSafe = device.safeRect();
  b.list.area = {deviceSafe.x, deviceSafe.y, deviceSafe.width, deviceSafe.height};
  b.list.font = fonts.bodyFontId;
  b.small_font = fonts.smallFontId;
  b.list.row_height = mappedInput.hasTouch() ? 0 : m.listRowHeight;
  b.list.row_gap = m.listRowGap;
  b.list.inset = m.listInset;
  b.list.padding = m.listSidePadding;
  b.list.scroll_width = m.listScrollWidth;
  b.list.radius = m.listRowRadius;
  b.list.selection = m.listSelectionStyle;
  b.list.scroll_left = m.listScrollSide;
  b.list.bold = m.listTitleBold;
  b.top_padding = m.topPadding;
  b.header_height = m.headerHeight;
  b.tab_height = m.tabBarHeight;
  b.spacing = m.verticalSpacing;
  b.side_padding = m.contentSidePadding;
  b.touch = mappedInput.hasTouch();
  b.preview_height = renderer.getLineHeight(fonts.bodyFontId) * 2 + 8;
  b.fixed_font = UI_10_FONT_ID;
  b.help_font = SMALL_FONT_ID;
  b.flags = renderingState.list.selected >= 0 && static_cast<size_t>(renderingState.list.selected) < networks.size() &&
            networks[renderingState.list.selected].hasSavedPassword;
  layout.legacy_body_font = UI_12_FONT_ID;
  layout.popup_border = m.popupFrameThickness;
  layout.popup_radius = m.popupCornerRadius;
  if (renderingState.phase == XP_UI_WIFI_PHASE_SAVE || renderingState.phase == XP_UI_WIFI_PHASE_FORGET)
    promptLabel.emplace(std::string(tr(STR_NETWORK_PREFIX)) + selectedSSID);
  XpUiWifiBudget budget{};
  if (!xp_ui_network_wifi_budget(&renderingState, &layout, renderer.getLineHeight(fonts.bodyFontId), &budget))
    return false;
  if (budget.commands == 0) {
    scene = {};
    scene.refresh = XP_UI_NONE;
    next = renderingState.list;
    nextHits = {};
    return true;
  }
  if (budget.rows > rowCapacity) {
    auto rows = makeUniqueNoThrow<VisibleRow[]>(budget.rows);
    if (!rows) {
      LOG_ERR("WIFI", "Cannot allocate %u snapshot bytes", static_cast<unsigned>(budget.rows * sizeof(VisibleRow)));
      return false;
    }
    visibleRows = std::move(rows);
    rowCapacity = budget.rows;
  }
  if (renderingState.phase == XP_UI_WIFI_PHASE_LIST) retireRequested = false;
  for (uint16_t i = 0; i < rowCapacity; ++i) visibleRows[i].index = UINT16_MAX;
  if (budget.commands > commandCapacity) {
    auto arena = makeUniqueNoThrow<XpUiCommand[]>(budget.commands);
    if (!arena) {
      LOG_ERR("WIFI", "Cannot allocate %u scene bytes", static_cast<unsigned>(budget.commands * sizeof(XpUiCommand)));
      return false;
    }
    commands = std::move(arena);
    commandCapacity = budget.commands;
  }
  const auto service = resources();
  const XpUiBuffer buffer{commands.get(), nullptr, commandCapacity, 0};
  return xp_ui_network_wifi_view(&renderingState, &layout, &service, &buffer, &next, &scene, &nextHits) == 1;
}

void WifiSelectionActivity::render(RenderLock&&) {
  bool painted = false;
  uint8_t refresh = XP_UI_NONE;
  {
    ResourceLock lock(*this);
    promptLabel.reset();
    paintingIp.reset();
    paintingSsid.reset();
    XpUiScene scene{};
    if (buildRustScene(scene)) {
      if (scene.command_count == 0) return;
      refresh = scene.refresh;
      if (renderingState.phase == XP_UI_WIFI_PHASE_LIST) {
        paintingRows = true;
        lock.release();
      }
      scene.refresh = XP_UI_NONE;
      painted = paintRustScene(scene);
      if (renderingState.phase == XP_UI_WIFI_PHASE_LIST) {
        ResourceLock finish(*this);
        paintingRows = false;
        if (retireRequested) {
          visibleRows.reset();
          rowCapacity = 0;
        }
      }
    } else {
      rejectRustScene();
    }
    paintingIp.reset();
    paintingSsid.reset();
    promptLabel.reset();
  }
  if (painted && refresh != XP_UI_NONE) renderer.displayBuffer();
}
XpUiPaint WifiSelectionActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* context, uint32_t handle, uint32_t generation) -> uint8_t {
            if (generation != 1) return 0;
            if (handle >= XP_UI_WIFI_HEADER && handle <= XP_UI_WIFI_PROMPT_FORGET) return 1;
            return static_cast<WifiSelectionActivity*>(context)->text(handle) != nullptr;
          },
          [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<WifiSelectionActivity*>(context);
            using networkui::Kind;
            networkui::Resource resource{Kind::Text};
            switch (command->resource) {
              case XP_UI_WIFI_HEADER: {
                char count[64];
                snprintf(count, sizeof(count), tr(STR_NETWORKS_FOUND),
                         static_cast<size_t>(self.renderingState.real_count));
                networkui::paint(self.renderer, self.mappedInput, *command, clip,
                                 {Kind::Header, {tr(STR_WIFI_NETWORKS), count}});
                return;
              }
              case XP_UI_WIFI_SUBHEADER:
                resource = {Kind::Subheader, {self.renderingMac}};
                break;
              case XP_UI_WIFI_LIST_FOOTER:
                resource = {Kind::Footer, {tr(STR_BACK), tr(STR_CONNECT), "", tr(STR_RETRY)}};
                break;
              case XP_UI_WIFI_SAVED_FOOTER:
                resource = {Kind::Footer, {tr(STR_BACK), tr(STR_CONNECT), tr(STR_FORGET_BUTTON), tr(STR_RETRY)}};
                break;
              case XP_UI_WIFI_AUTO_FOOTER:
                resource = {Kind::Footer, {tr(STR_CANCEL), tr(STR_SHOW_NETWORKS), "", ""}};
                break;
              case XP_UI_WIFI_SAVE_FOOTER:
                self.promptLabel.reset();
                resource = {Kind::Footer, {tr(STR_CANCEL), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT)}};
                break;
              case XP_UI_WIFI_FORGET_FOOTER:
                self.promptLabel.reset();
                resource = {Kind::Footer, {tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT)}};
                break;
              case XP_UI_WIFI_CONNECTED_FOOTER:
                resource = {Kind::Footer, {"", tr(STR_DONE), "", ""}};
                break;
              case XP_UI_WIFI_FAILED_FOOTER:
                resource = {Kind::Footer, {tr(STR_BACK), tr(STR_DONE), "", ""}};
                break;
              case XP_UI_WIFI_LEGEND:
                resource = {Kind::Help, {tr(STR_NETWORK_LEGEND)}};
                break;
              case XP_UI_WIFI_SCANNING:
              case XP_UI_WIFI_FINDING_SAVED:
                resource = {Kind::Wrapped, {self.text(command->resource)}};
                break;
              case XP_UI_WIFI_CONNECTING:
              case XP_UI_WIFI_CONNECTING_SAVED:
                resource = {Kind::WrappedBottom, {self.text(command->resource)}};
                break;
              case XP_UI_WIFI_PROMPT_SSID:
              case XP_UI_WIFI_PROMPT_CONNECTED:
              case XP_UI_WIFI_PROMPT_FORGET:
                resource = {Kind::FlowText, {self.text(command->resource)}};
                break;
              case XP_UI_WIFI_TO_SSID:
              case XP_UI_WIFI_NETWORK_SSID: {
                self.paintingSsid.emplace(self.renderer.truncatedText(
                    command->font,
                    (std::string(command->resource == XP_UI_WIFI_TO_SSID ? tr(STR_TO_PREFIX) : tr(STR_NETWORK_PREFIX)) +
                     self.selectedSSID)
                        .c_str(),
                    command->rect.w));
                resource = {Kind::Centered, {self.paintingSsid->c_str()}};
                break;
              }
              case XP_UI_WIFI_IP:
                self.paintingIp.emplace(std::string(tr(STR_IP_ADDRESS_PREFIX)) + self.connectedIP);
                resource = {Kind::Centered, {self.paintingIp->c_str()}};
                break;
              default:
                resource.labels[0] = self.text(command->resource);
                if (command->max_lines == 0 && command->align == 1) resource.kind = Kind::Centered;
                break;
            }
            networkui::paint(self.renderer, self.mappedInput, *command, clip, resource);
          }};
}
