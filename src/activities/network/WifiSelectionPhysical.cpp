#include <HalClock.h>
#include <I18n.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <new>

#include "CrossPointSettings.h"
#include "NetworkUiBinding.h"
#include "WifiSelectionActivity.h"
#include "activities/OwnedLegacyActivity.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/PluginEvents.h"

struct WifiPhysical {
  static constexpr uint32_t ERROR_GENERAL = 0x200000;
  static constexpr uint32_t ERROR_MISSING = 0x200001;
  static constexpr uint32_t ERROR_TIMEOUT = 0x200002;
  static constexpr uint32_t HIDDEN_LABEL = 0x200003;
  using Last = std::optional<std::string>;
  using Credential = std::optional<WifiCredential>;
  static WifiSelectionActivity& get(void* object) {
    return *static_cast<WifiSelectionActivity*>(nativeActivity(object));
  }
  static XpWifiText text(const std::string& s) { return {&s, {reinterpret_cast<const uint8_t*>(s.data()), s.size()}}; }
  static const std::string& string(XpWifiText s) { return *static_cast<const std::string*>(s.string); }
  static void assign(std::string& target, const XpWifiText* value) {
    if (!value) return;
    if (value->string)
      target = string(*value);
    else if (!value->bytes.data)
      target.clear();
    else
      target.assign(reinterpret_cast<const char*>(value->bytes.data), value->bytes.len);
  }
  static void snapshot(void* object, uint32_t index, const XpWifiText* attempted, XpWifiSnapshot* out) {
    auto& s = get(object);
    *out = {};
    out->strings[XP_WIFI_SELECTED] = text(s.selectedSSID);
    out->strings[XP_WIFI_IP] = text(s.connectedIP);
    out->strings[XP_WIFI_ERROR] = text(s.connectionError);
    out->strings[XP_WIFI_PASSWORD] = text(s.enteredPassword);
    out->strings[XP_WIFI_MAC] = text(s.cachedMacAddress);
    out->row_count = s.networks.size();
    if (index < s.networks.size()) {
      const auto& n = s.networks[index];
      out->row = {text(n.ssid), n.rssi, n.isEncrypted, n.hasSavedPassword, n.isHiddenPlaceholder};
      out->row_valid = true;
      if (index < s.networkStatuses.size()) {
        const auto& status = s.networkStatuses[index];
        out->status = {reinterpret_cast<const uint8_t*>(status.data()), status.size()};
      }
    }
    if (attempted)
      out->attempted = std::find(s.autoAttemptedSsids.begin(), s.autoAttemptedSsids.end(), string(*attempted)) !=
                       s.autoAttemptedSsids.end();
  }
  static void layout(void* object, XpUiWifiLayout* out);
  static void paint(void* object, const XpUiCommand* command, XpUiRect clip, const XpWifiPaintSnapshot* snapshot);
  static void diagnostic(void* object, XpWifiDiagnostic event, uint32_t bytes);
  static constexpr XpWifiPhysical table() {
    return {[](void* o) -> uint8_t { return get(o).allowAutoConnect; },
            [](void* o, uint8_t acquire) {
              if (acquire)
                xSemaphoreTake(get(o).resourceMutex, portMAX_DELAY);
              else
                xSemaphoreGive(get(o).resourceMutex);
            },
            [](void* o) { get(o).releaseResources(); },
            layout,
            [](void* o) {
              auto out = get(o).resources().rows;
              out.count = get(o).networkStatuses.size();
              return out;
            },
            [](void* o, uint32_t id) {
              switch (id) {
                case ERROR_GENERAL:
                  return tr(STR_ERROR_GENERAL_FAILURE);
                case ERROR_MISSING:
                  return tr(STR_ERROR_NETWORK_NOT_FOUND);
                case ERROR_TIMEOUT:
                  return tr(STR_ERROR_CONNECTION_TIMEOUT);
                case HIDDEN_LABEL:
                  return tr(STR_ADD_HIDDEN_NETWORK);
                default:
                  return get(o).text(id);
              }
            },
            paint,
            [](void* o) {
              auto& s = get(o);
              s.paintingIp.reset();
              s.paintingSsid.reset();
              s.promptLabel.reset();
            },
            [](void* o) {
              auto& s = get(o);
              s.promptLabel.emplace(std::string(tr(STR_NETWORK_PREFIX)) + s.selectedSSID);
            },
            [](void* o) {
              uint8_t mac[6] = {};
              char macStr[64];
              const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
              if (macResult == ESP_OK) {
                snprintf(macStr, sizeof(macStr), "%s %02x-%02x-%02x-%02x-%02x-%02x", tr(STR_MAC_ADDRESS), mac[0],
                         mac[1], mac[2], mac[3], mac[4], mac[5]);
              } else {
                LOG_ERR("WIFI", "Failed to read station MAC (err=%d)", static_cast<int>(macResult));
                snprintf(macStr, sizeof(macStr), "%s --", tr(STR_MAC_ADDRESS));
              }
              get(o).cachedMacAddress = std::string(macStr);
            },
            [](XpWifiLastSsidStorage* slot) { reinterpret_cast<Last*>(slot)->~Last(); },
            [](XpWifiLastSsidStorage* slot) {
              auto& last = *new (slot) Last{};
              last.emplace(WIFI_STORE.getLastConnectedSsid());
              return text(*last);
            },
            [](XpWifiCredentialStorage* slot, XpWifiText ssid, XpWifiCredentialView* out) -> uint8_t {
              auto& credential = *new (slot) Credential{};
              credential = WIFI_STORE.findCredential(string(ssid));
              if (!credential) {
                *out = {};
                return false;
              }
              *out = {text(credential->ssid), text(credential->password)};
              return true;
            },
            [](XpWifiCredentialStorage* slot) { reinterpret_cast<Credential*>(slot)->~Credential(); },
            [] { WIFI_STORE.loadFromFile(); },
            [](XpWifiText ssid, XpWifiText password) { WIFI_STORE.addCredential(string(ssid), string(password)); },
            [](void* o, XpWifiText ssid) {
              WIFI_STORE.removeCredential(string(ssid));
              auto& rows = get(o).networks;
              auto n = std::find_if(rows.begin(), rows.end(),
                                    [&](const WifiNetworkInfo& n) { return n.ssid == string(ssid); });
              if (n != rows.end()) n->hasSavedPassword = false;
            },
            [](XpWifiText ssid) { WIFI_STORE.setLastConnectedSsid(string(ssid)); },
            snapshot,
            [](void* o, const XpWifiTextEdits* edits) {
              auto& s = get(o);
              if (edits->attempt) s.autoAttemptedSsids.push_back(string(*edits->attempt));
              assign(s.selectedSSID, edits->assign[0]);
              assign(s.connectedIP, edits->assign[1]);
              assign(s.connectionError, edits->assign[2]);
              assign(s.enteredPassword, edits->assign[3]);
            },
            [](void* o, uint32_t reserve, uint8_t statuses) {
              auto& s = get(o);
              s.networks.clear();
              if (statuses) s.networkStatuses.clear();
              s.networks.reserve(reserve);
            },
            [](void* o, uint32_t target, const XpWifiRow* row, const uint32_t* scan) {
              auto& s = get(o);
              if (target != UINT32_MAX) {
                auto& n = s.networks[target];
                n.rssi = row->rssi;
                n.isEncrypted = WiFi.encryptionType(*scan) != WIFI_AUTH_OPEN;
                return;
              }
              WifiNetworkInfo n;
              if (scan) {
                assign(n.ssid, &row->ssid);
                n.rssi = row->rssi;
                n.isEncrypted = WiFi.encryptionType(*scan) != WIFI_AUTH_OPEN;
                n.hasSavedPassword = WIFI_STORE.hasSavedCredential(n.ssid);
              } else {
                n.rssi = row->rssi;
                n.isEncrypted = row->encrypted;
                n.hasSavedPassword = row->saved;
                n.isHiddenPlaceholder = row->hidden;
              }
              s.networks.push_back(std::move(n));
            },
            [](void* o, uint8_t (*before)(uint8_t, int32_t, uint8_t, int32_t)) {
              auto& n = get(o).networks;
              std::sort(n.begin(), n.end(), [before](const WifiNetworkInfo& a, const WifiNetworkInfo& b) {
                return before(a.hasSavedPassword, a.rssi, b.hasSavedPassword, b.rssi) != 0;
              });
            },
            [](void* o, uint8_t (*strength)(int32_t)) {
              auto& s = get(o);
              s.networkStatuses.assign(s.networks.size(), std::string());
              static constexpr const char* bars[] = {"||||", " |||", "  ||", "   |"};
              for (size_t i = 0; i < s.networks.size(); ++i) {
                const auto& n = s.networks[i];
                if (!n.isHiddenPlaceholder)
                  s.networkStatuses[i] = std::string(n.hasSavedPassword ? "+ " : "") + (n.isEncrypted ? "* " : "") +
                                         std::string(bars[strength(n.rssi)]);
              }
            },
            [](void* o) -> uint32_t {
              auto& a = get(o).autoAttemptedSsids;
              a.clear();
              const auto count = WIFI_STORE.getCredentialCount();
              a.reserve(count);
              return count;
            },
            [] { WiFi.mode(WIFI_STA); },
            [] { WiFi.disconnect(); },
            [] { WiFi.scanNetworks(true); },
            [] { return static_cast<int32_t>(WiFi.scanComplete()); },
            [](uint32_t i, char out[33]) { strlcpy(out, WiFi.SSID(i).c_str(), 33); },
            [](uint32_t i) { return WiFi.RSSI(i); },
            [] { WiFi.scanDelete(); },
            [](XpWifiText ssid, XpWifiText password, uint8_t usePassword) {
              WiFi.persistent(false);
              WiFi.mode(WIFI_STA);
              WiFi.disconnect(true, true);
              delay(100);
              WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
              WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
              uint8_t mac[6] = {};
              const esp_err_t macResult = esp_read_mac(mac, ESP_MAC_WIFI_STA);
              if (macResult == ESP_OK) {
                char hostname[sizeof("CrossPoint-Reader-") + 12];
                snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
                         mac[2], mac[3], mac[4], mac[5]);
                WiFi.setHostname(hostname);
              } else
                LOG_ERR("WIFI", "Failed to read station MAC for hostname (err=%d)", static_cast<int>(macResult));
              if (usePassword)
                WiFi.begin(string(ssid).c_str(), string(password).c_str());
              else
                WiFi.begin(string(ssid).c_str());
            },
            [] { return static_cast<int32_t>(WiFi.status()); },
            [](char out[16]) {
              IPAddress ip = WiFi.localIP();
              snprintf(out, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
            },
            [] {
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
              uint8_t connectedBssid[6] = {};
              WiFi.BSSID(connectedBssid);
              LOG_DBG("WIFI", "Connected BSSID: %02x:%02x:%02x:%02x:%02x:%02x, channel: %d, RSSI: %d dBm",
                      static_cast<unsigned>(connectedBssid[0]), static_cast<unsigned>(connectedBssid[1]),
                      static_cast<unsigned>(connectedBssid[2]), static_cast<unsigned>(connectedBssid[3]),
                      static_cast<unsigned>(connectedBssid[4]), static_cast<unsigned>(connectedBssid[5]),
                      WiFi.channel(), WiFi.RSSI());
#endif
            },
            []() -> uint8_t { return halClock.isAvailable(); },
            []() -> uint8_t { return SETTINGS.clockHasBeenSynced; },
            []() -> uint8_t { return halClock.syncFromNTP(); },
            [] {
              SETTINGS.clockHasBeenSynced = 1;
              SETTINGS.saveToFile();
            },
            [] { trustedtime::startSync(); },
            [](void* o) { pluginevents::drain(&get(o).renderer); },
            diagnostic,
            [](void* o, uint8_t connected) {
              auto& s = get(o);
              ActivityResult result;
              result.isCancelled = !connected;
              if (connected) result.data = WifiResult{true, s.selectedSSID, s.connectedIP};
              s.setResult(std::move(result));
            },
            [](XpWifiKeyboardStorage* slot, XpAppText bytes) {
              auto& s = *new (slot) std::string(
                  bytes.len ? std::string(reinterpret_cast<const char*>(bytes.data), bytes.len) : std::string());
              return text(s);
            },
            [](XpWifiKeyboardStorage* slot) { reinterpret_cast<std::string*>(slot)->~basic_string(); }};
  }
};
const XpWifiPhysical XP_WIFI_PHYSICAL = WifiPhysical::table();
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static_assert(sizeof(WifiPhysical::Last) == sizeof(XpWifiLastSsidStorage));
static_assert(sizeof(WifiPhysical::Credential) == sizeof(XpWifiCredentialStorage));
static_assert(sizeof(std::string) == sizeof(XpWifiKeyboardStorage));
static_assert(sizeof(XpWifiSnapshot) == 96);
static_assert(sizeof(XpWifiPhysical) == 184);
#endif

void WifiPhysical::layout(void* object, XpUiWifiLayout* out) {
  auto& self = get(object);
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = self.textTarget.deviceContext();
  const auto safe = UITheme::getInstance().getScreenSafeArea(self.renderer, true, false);
  XpUiWifiLayout layout{};
  auto& b = layout.base;
  b.list.screen = {0, 0, device.width, device.height};
  b.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
            static_cast<int16_t>(safe.height)};
  const auto deviceSafe = device.safeRect();
  b.list.area = {deviceSafe.x, deviceSafe.y, deviceSafe.width, deviceSafe.height};
  b.list.font = fonts.bodyFontId;
  b.small_font = fonts.smallFontId;
  b.list.row_height = self.mappedInput.hasTouch() ? 0 : m.listRowHeight;
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
  b.touch = self.mappedInput.hasTouch();
  b.preview_height = self.renderer.getLineHeight(fonts.bodyFontId) * 2 + 8;
  b.fixed_font = UI_10_FONT_ID;
  b.help_font = SMALL_FONT_ID;
  layout.legacy_body_font = UI_12_FONT_ID;
  layout.popup_border = m.popupFrameThickness;
  layout.popup_radius = m.popupCornerRadius;
  *out = layout;
}

void WifiPhysical::paint(void* object, const XpUiCommand* command, XpUiRect clip, const XpWifiPaintSnapshot* snapshot) {
  auto& self = get(object);
  using networkui::Kind;
  networkui::Resource resource{Kind::Text};
  switch (command->resource) {
    case XP_UI_WIFI_HEADER: {
      char count[64];
      snprintf(count, sizeof(count), tr(STR_NETWORKS_FOUND), static_cast<size_t>(snapshot->real_count));
      networkui::paint(self.renderer, self.mappedInput, *command, clip, {Kind::Header, {tr(STR_WIFI_NETWORKS), count}});
      return;
    }
    case XP_UI_WIFI_SUBHEADER:
      resource = {Kind::Subheader, {snapshot->mac}};
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
      resource.labels[0] = command->resource < XP_UI_WIFI_HEADER ? snapshot->row_text : self.text(command->resource);
      if (command->max_lines == 0 && command->align == 1) resource.kind = Kind::Centered;
      break;
  }
  networkui::paint(self.renderer, self.mappedInput, *command, clip, resource);
}

void WifiPhysical::diagnostic(void* object, XpWifiDiagnostic event, uint32_t bytes) {
  switch (event) {
    case XP_WIFI_LOG_EXIT_START:
      LOG_DBG("WIFI", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());
      break;
    case XP_WIFI_LOG_SCAN_DELETE:
      LOG_DBG("WIFI", "Deleting WiFi scan...");
      break;
    case XP_WIFI_LOG_SCAN_DELETED:
      LOG_DBG("WIFI", "Free heap after scanDelete: %d bytes", ESP.getFreeHeap());
      break;
    case XP_WIFI_LOG_EXIT_END:
      LOG_DBG("WIFI", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
      break;
    case XP_WIFI_LOG_AUTO_ATTEMPT:
      // This diagnostic borrows the credential string before selectedSSID is assigned.
      LOG_DBG("WIFI", "Attempting saved network: %s", static_cast<const std::string*>(object)->c_str());
      break;
    case XP_WIFI_LOG_SAVED:
      LOG_DBG("WiFi", "Using saved password for %s, length: %zu", get(object).selectedSSID.c_str(),
              get(object).enteredPassword.size());
      break;
    case XP_WIFI_LOG_HIDDEN_SAVED:
      LOG_DBG("WiFi", "Using saved password for hidden network %s", get(object).selectedSSID.c_str());
      break;
    case XP_WIFI_LOG_CONNECTED:
      LOG_DBG("WIFI", "Connected with saved/open credentials, completing immediately");
      break;
    case XP_WIFI_LOG_AUTO_FAILURE:
      LOG_DBG("WIFI", "Saved network failed: %s", get(object).selectedSSID.c_str());
      break;
    case XP_WIFI_LOG_MANUAL:
      LOG_DBG("WIFI", "User requested manual network list");
      break;
    case XP_WIFI_LOG_SNAPSHOT_OOM:
      LOG_ERR("WIFI", "Cannot allocate %u snapshot bytes", static_cast<unsigned>(bytes));
      break;
    case XP_WIFI_LOG_SCENE_OOM:
      LOG_ERR("WIFI", "Cannot allocate %u scene bytes", static_cast<unsigned>(bytes));
      break;
    case XP_WIFI_LOG_INVALID_SCENE:
      LOG_ERR("RUI", "Invalid scene in %s", "WifiSelection");
      break;
  }
}
