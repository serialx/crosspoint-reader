#include <HalClock.h>
#include <I18n.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <xp_network_owner.h>

#include <new>
#include <string_view>

#include "CrossPointSettings.h"
#include "NetworkUiBinding.h"
#include "WifiCredentialStore.h"
#include "WifiSelectionActivity.h"
#include "activities/OwnedLegacyActivity.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/PluginEvents.h"

struct WifiPhysical {
  using Last = std::optional<std::string>;
  using Credential = std::optional<WifiCredential>;
  static WifiSelectionActivity& get(void* object) {
    return *static_cast<WifiSelectionActivity*>(nativeActivity(object));
  }
  static XpAppText bytes(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
  static std::string_view view(XpAppText s) {
    return s.len ? std::string_view(reinterpret_cast<const char*>(s.data), s.len) : std::string_view();
  }
  static void layout(void*, XpUiWifiLayout*);
  static void paint(void*, const XpUiCommand*, XpUiRect, const XpWifiPaintData*);
  static void diagnostic(XpWifiDiagnostic, XpAppText, uint32_t);
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
            [](void* o, const char* text, int32_t font, uint8_t bold) -> int32_t {
              return get(o).renderer.getTextWidth(font, text, static_cast<EpdFontFamily::Style>(bold));
            },
            [](void* o, const char* text, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
              auto& target = get(o).textTarget;
              target.setFont(3, font);
              freeink::ui::TextStyle style;
              style.font = 3;
              style.bold = bold;
              style.maxLines = lines;
              return freeink::ui::measureWrappedText(target, text, style, width).height;
            },
            [](void* o, int32_t font) -> int32_t { return get(o).renderer.getLineHeight(font); },
            paint,
            [](uint8_t out[6]) -> int32_t {
              const auto result = esp_read_mac(out, ESP_MAC_WIFI_STA);
              if (result != ESP_OK) LOG_ERR("WIFI", "Failed to read station MAC (err=%d)", static_cast<int>(result));
              return result;
            },
            [](XpWifiLastSsidStorage* slot) { reinterpret_cast<Last*>(slot)->~Last(); },
            [](XpWifiLastSsidStorage* slot) {
              const auto& last = *new (slot) Last{WIFI_STORE.getLastConnectedSsid()};
              return bytes(*last);
            },
            [](XpWifiCredentialStorage* slot, XpAppText ssid, XpWifiCredentialView* out) -> uint8_t {
              const auto& credential = *new (slot) Credential(WIFI_STORE.findCredential(view(ssid)));
              if (!credential) {
                *out = {};
                return false;
              }
              *out = {bytes(credential->ssid), bytes(credential->password)};
              return true;
            },
            [](XpWifiCredentialStorage* slot) { reinterpret_cast<Credential*>(slot)->~Credential(); },
            [] { WIFI_STORE.loadFromFile(); },
            [](XpAppText ssid, XpAppText password) { WIFI_STORE.addCredential(view(ssid), view(password)); },
            [](XpAppText ssid) { WIFI_STORE.removeCredential(view(ssid)); },
            [](XpAppText ssid) { WIFI_STORE.setLastConnectedSsid(view(ssid)); },
            []() -> uint32_t { return WIFI_STORE.getCredentialCount(); },
            [](XpAppText ssid) -> uint8_t { return WIFI_STORE.hasSavedCredential(view(ssid)); },
            [] { WiFi.mode(WIFI_STA); },
            [] { WiFi.disconnect(); },
            [] { WiFi.scanNetworks(true); },
            [] { return static_cast<int32_t>(WiFi.scanComplete()); },
            [](uint32_t i, char out[33]) { strlcpy(out, WiFi.SSID(i).c_str(), 33); },
            [](uint32_t i) { return WiFi.RSSI(i); },
            [](uint32_t i) -> uint8_t { return WiFi.encryptionType(i) != WIFI_AUTH_OPEN; },
            [] { WiFi.scanDelete(); },
            [](const char* ssid, const char* password, uint8_t usePassword) {
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
                WiFi.begin(ssid, password);
              else
                WiFi.begin(ssid);
            },
            [] {
#ifdef CROSSPOINT_UI_ORACLE
              return uiOracleWifiConnectionStatus(static_cast<int32_t>(WiFi.status()));
#else
              return static_cast<int32_t>(WiFi.status());
#endif
            },
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
            [](void* o, uint8_t connected, XpAppText ssid, XpAppText ip) {
              ActivityResult result;
              result.isCancelled = !connected;
              if (connected) result.data = WifiResult{true, std::string(view(ssid)), std::string(view(ip))};
              get(o).setResult(std::move(result));
            }};
  }
};
const XpWifiPhysical XP_WIFI_PHYSICAL = WifiPhysical::table();
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static_assert(sizeof(WifiPhysical::Last) == sizeof(XpWifiLastSsidStorage));
static_assert(sizeof(WifiPhysical::Credential) == sizeof(XpWifiCredentialStorage));
static_assert(sizeof(XpWifiCredentialView) == 16);
static_assert(sizeof(XpWifiPaintData) == 20);
static_assert(sizeof(XpWifiPhysical) == 156);
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
  layout.language = static_cast<uint8_t>(I18N.getLanguage());
  *out = layout;
}

void WifiPhysical::paint(void* object, const XpUiCommand* command, XpUiRect clip, const XpWifiPaintData* data) {
  using networkui::Kind;
  Kind kind = Kind::Text;
  switch (static_cast<XpWifiDrawKind>(data->kind)) {
    case XP_WIFI_DRAW_TEXT:
      kind = Kind::Text;
      break;
    case XP_WIFI_DRAW_CENTERED:
      kind = Kind::Centered;
      break;
    case XP_WIFI_DRAW_FLOW_TEXT:
      kind = Kind::FlowText;
      break;
    case XP_WIFI_DRAW_WRAPPED:
      kind = Kind::Wrapped;
      break;
    case XP_WIFI_DRAW_WRAPPED_BOTTOM:
      kind = Kind::WrappedBottom;
      break;
    case XP_WIFI_DRAW_HEADER:
      kind = Kind::Header;
      break;
    case XP_WIFI_DRAW_SUBHEADER:
      kind = Kind::Subheader;
      break;
    case XP_WIFI_DRAW_FOOTER:
      kind = Kind::Footer;
      break;
    case XP_WIFI_DRAW_HELP:
      kind = Kind::Help;
      break;
  }
  auto& self = get(object);
  networkui::paint(self.renderer, self.mappedInput, *command, clip,
                   {kind, {data->labels[0], data->labels[1], data->labels[2], data->labels[3]}});
}

void WifiPhysical::diagnostic(XpWifiDiagnostic event, XpAppText text, uint32_t bytes) {
  const char* label = text.len ? reinterpret_cast<const char*>(text.data) : "";
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
      LOG_DBG("WIFI", "Attempting saved network: %.*s", static_cast<int>(text.len), label);
      break;
    case XP_WIFI_LOG_SAVED:
      LOG_DBG("WiFi", "Using saved password for %.*s, length: %zu", static_cast<int>(text.len), label,
              static_cast<size_t>(bytes));
      break;
    case XP_WIFI_LOG_HIDDEN_SAVED:
      LOG_DBG("WiFi", "Using saved password for hidden network %.*s", static_cast<int>(text.len), label);
      break;
    case XP_WIFI_LOG_CONNECTED:
      LOG_DBG("WIFI", "Connected with saved/open credentials, completing immediately");
      break;
    case XP_WIFI_LOG_AUTO_FAILURE:
      LOG_DBG("WIFI", "Saved network failed: %.*s", static_cast<int>(text.len), label);
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
