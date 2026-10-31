#include "CrossPointWebServerActivity.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <WiFi.h>

#include <cassert>

#include "MappedInputManager.h"
#include "NetworkUiBinding.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "activities/network/CalibreConnectActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/PluginEvents.h"
#include "util/QrUtils.h"
#include "util/TaskWatchdog.h"
namespace {
constexpr const char* AP_SSID = "CrossPoint-Reader";
constexpr const char* AP_PASSWORD = nullptr;
constexpr const char* AP_HOSTNAME = "crosspoint";
constexpr uint8_t AP_CHANNEL = 1, AP_MAX_CONNECTIONS = 4;
constexpr uint16_t DNS_PORT = 53;
constexpr uint8_t CALIBRE_RESULT = 5;
DNSServer* dnsServer = nullptr;
void stopDnsServer() {
  if (!dnsServer) return;

  dnsServer->stop();
  delete dnsServer;
  dnsServer = nullptr;
}
void restartMdns(const char* hostname, const char* tag) {
  MDNS.end();
  if (MDNS.begin(hostname)) {
    LOG_DBG(tag, "mDNS started: http://%s.local/", hostname);
  } else {
    LOG_DBG(tag, "WARNING: mDNS failed to start");
  }
}
}  // namespace
struct CrossPointWebServerActivity::CallContext {
  CrossPointWebServerActivity& activity;
  IPAddress apIP;
  wl_status_t wifiStatus = WL_CONNECTED;
};
void CrossPointWebServerActivity::onEnter() {
  Activity::onEnter();
  run(XP_UI_NETWORK_ENTER);
}
void CrossPointWebServerActivity::onExit() {
  Activity::onExit();
  run(XP_UI_NETWORK_EXIT);
}
void CrossPointWebServerActivity::loop() {
  consumeRejectedInput();
  // Transport progress and the existing cancel policy continue during rejection.
  run(XP_UI_NETWORK_TICK);
}
void CrossPointWebServerActivity::onNetworkModeSelected(NetworkMode mode) {
  run(XP_UI_NETWORK_INPUT, static_cast<uint8_t>(mode));
}
void CrossPointWebServerActivity::onWifiSelectionComplete(bool connected) {
  run(XP_UI_NETWORK_CHILD_RESULT, connected);
}
void CrossPointWebServerActivity::run(uint8_t event, uint8_t value) {
  assert(!runningPolicy);
  if (runningPolicy) return;
  runningPolicy = true;
  XpUiWebState next;
  {
    ModelLock lock(*this);
    next = model;
  }
  CallContext context{*this, {}};
  const XpUiWebServices services{&context, service, [](void* context, const XpUiWebState* state) {
                                   auto& self = static_cast<CallContext*>(context)->activity;
                                   ModelLock lock(self);
                                   self.model = *state;
                                 }};
  xp_ui_network_web_run(&next, &services, event, value);
  runningPolicy = false;
}
uint32_t CrossPointWebServerActivity::service(void* context, uint32_t operation, uint32_t value) {
  auto& call = *static_cast<CallContext*>(context);
  auto& self = call.activity;
  switch (static_cast<XpUiWebCall>(operation)) {
    case XP_UI_WEB_NOW:
      return millis();
    case XP_UI_WEB_PAINT:
      self.requestUpdate();
      return 0;
    case XP_UI_WEB_ENTER_RESOURCES:
      LOG_DBG("WEBACT", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());
      if (auto* fcm = self.renderer.getFontCacheManager()) {
        fcm->releaseSdFontCaches();
        LOG_DBG("WEBACT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
      }
      return 0;
    case XP_UI_WEB_EXIT_LOG_START:
      LOG_DBG("WEBACT", "Free heap at onExit start: %d bytes", ESP.getFreeHeap());
      return 0;
    case XP_UI_WEB_EXIT_LOG_END:
      LOG_DBG("WEBACT", "Free heap at onExit end: %d bytes", ESP.getFreeHeap());
      return 0;
    case XP_UI_WEB_DNS_STOP:
      stopDnsServer();
      return 0;
    case XP_UI_WEB_MDNS_END:
      MDNS.end();
      return 0;
    case XP_UI_WEB_PLUGINS_REFRESH:
      pluginevents::refreshSubscriptions();
      return 0;
    case XP_UI_WEB_WIFI_PRESENT:
      return WiFi.getMode() != WIFI_MODE_NULL;
    case XP_UI_WEB_WIFI_DISCONNECT:
      WiFi.disconnect(false);
      return 0;
    case XP_UI_WEB_AP_DISCONNECT:
      WiFi.softAPdisconnect(true);
      return 0;
    case XP_UI_WEB_DELAY:
      delay(value);
      return 0;
    case XP_UI_WEB_RESTART:
      silentRestart();
      return 0;
    case XP_UI_WEB_RESTART_JOIN:
      silentRestartToJoinNetwork();
      return 0;
    case XP_UI_WEB_LOG_MODE: {
      const char* modeName = "Join Network";
      if (value == static_cast<uint8_t>(NetworkMode::CONNECT_CALIBRE))
        modeName = "Connect to Calibre";
      else if (value == static_cast<uint8_t>(NetworkMode::CREATE_HOTSPOT))
        modeName = "Create Hotspot";
#if FREEINK_CAP_USB_MSC
      else if (value == static_cast<uint8_t>(NetworkMode::USB_DRIVE))
        modeName = "USB Drive";
#endif
      LOG_DBG("WEBACT", "Network mode selected: %s", modeName);
      return 0;
    }
    case XP_UI_WEB_HOME:
      self.onGoHome();
      return 0;
    case XP_UI_WEB_SELECT_MODES:
      if (value) LOG_DBG("WEBACT", "Launching NetworkModeSelectionActivity...");
      self.startActivityForResult(std::make_unique<NetworkModeSelectionActivity>(self.renderer, self.mappedInput),
                                  [&self](const ActivityResult& result) {
                                    if (result.isCancelled)
                                      self.run(XP_UI_NETWORK_INPUT, 255);
                                    else
                                      self.onNetworkModeSelected(std::get<NetworkModeResult>(result.data).mode);
                                  });
      return 0;
    case XP_UI_WEB_SELECT_CALIBRE:
      self.startActivityForResult(std::make_unique<CalibreConnectActivity>(self.renderer, self.mappedInput),
                                  [&self](const ActivityResult&) { self.run(CALIBRE_RESULT); });
      return 0;
    case XP_UI_WEB_WIFI_STA:
      LOG_DBG("WEBACT", "Turning on WiFi (STA mode)...");
      WiFi.mode(WIFI_STA);
      return 0;
    case XP_UI_WEB_SELECT_WIFI:
      LOG_DBG("WEBACT", "Launching WifiSelectionActivity...");
      self.startActivityForResult(std::make_unique<WifiSelectionActivity>(self.renderer, self.mappedInput),
                                  [&self](const ActivityResult& result) {
                                    if (!result.isCancelled) {
                                      const auto& wifi = std::get<WifiResult>(result.data);
                                      ResourceLock lock(self);
                                      self.connectedIP = wifi.ip;
                                      self.connectedSSID = wifi.ssid;
                                    }
                                    self.onWifiSelectionComplete(!result.isCancelled);
                                  });
      return 0;
    case XP_UI_WEB_AP_START_LOG:
      LOG_DBG("WEBACT", "Starting Access Point mode...");
      LOG_DBG("WEBACT", "Free heap before AP start: %d bytes", ESP.getFreeHeap());
      return 0;
    case XP_UI_WEB_WIFI_AP:
      WiFi.mode(WIFI_AP);
      return 0;
    case XP_UI_WEB_AP_BEGIN:
      if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8)
        return WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
      return WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL, false, AP_MAX_CONNECTIONS);
    case XP_UI_WEB_AP_DETAILS: {
      const IPAddress apIP = WiFi.softAPIP();
      call.apIP = apIP;
      char ipStr[16];
      snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", apIP[0], apIP[1], apIP[2], apIP[3]);
      {
        ResourceLock lock(self);
        self.connectedIP = ipStr;
        self.connectedSSID = AP_SSID;
      }
      LOG_DBG("WEBACT", "Access Point started!");
      LOG_DBG("WEBACT", "SSID: %s", AP_SSID);
      LOG_DBG("WEBACT", "IP: %s", self.connectedIP.c_str());
      return 0;
    }
    case XP_UI_WEB_MDNS_RESTART:
      restartMdns(AP_HOSTNAME, "WEBACT");
      return 0;
    case XP_UI_WEB_DNS_START:
      stopDnsServer();
      dnsServer = new DNSServer();
      dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
      dnsServer->start(DNS_PORT, "*", call.apIP);
      LOG_DBG("WEBACT", "DNS server started for captive portal");
      return 0;
    case XP_UI_WEB_AP_READY_LOG:
      LOG_DBG("WEBACT", "Free heap after AP start: %d bytes", ESP.getFreeHeap());
      return 0;
    case XP_UI_WEB_SERVER_BEGIN:
      LOG_DBG("WEBACT", "Starting web server...");
      if (auto* fcm = self.renderer.getFontCacheManager()) {
        LOG_DBG("WEBACT", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
        fcm->releaseSdFontCaches();
        LOG_DBG("WEBACT", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
      }
      self.webServer.reset(new CrossPointWebServer());
      self.webServer->setUploadCancelCheck([&self] {
        self.mappedInput.update(true);
        if (gpio.rawInputActive()) {
          delay(6);
          self.mappedInput.update(true);
        }
        XpUiInput input{};
        if (self.mappedInput.isPressed(MappedInputManager::Button::Back)) input.held = XP_UI_BACK;
        if (self.mappedInput.wasPressed(MappedInputManager::Button::Back)) input.pressed = XP_UI_BACK;
        return xp_ui_network_web_cancel(&self.cancelLatch, &input, self.mappedInput.wasHomeGesture()) != 0;
      });
      self.webServer->begin();
      return 0;
    case XP_UI_WEB_SERVER_RUNNING:
      return self.webServer && self.webServer->isRunning();
    case XP_UI_WEB_SERVER_DELETE:
      self.webServer.reset();
      return 0;
    case XP_UI_WEB_SERVER_READY_LOG:
      LOG_DBG("WEBACT", "Web server started successfully");
      return 0;
    case XP_UI_WEB_SERVER_ERROR_LOG:
      LOG_ERR("WEBACT", "ERROR: Failed to start web server!");
      return 0;
    case XP_UI_WEB_WIFI_RSSI:
      return static_cast<uint32_t>(WiFi.RSSI());
    case XP_UI_WEB_PLUGIN_DRAIN:
      pluginevents::drain(&self.renderer);
      return 0;
    case XP_UI_WEB_DNS_NEXT:
      if (dnsServer) dnsServer->processNextRequest();
      return 0;
    case XP_UI_WEB_WIFI_STATUS:
      call.wifiStatus = WiFi.status();
      return call.wifiStatus == WL_CONNECTED;
    case XP_UI_WEB_INPUT_UPDATE:
      self.mappedInput.update(true);
      return 0;
    case XP_UI_WEB_EXIT_INPUT:
      return self.mappedInput.wasReleased(MappedInputManager::Button::Back) || self.mappedInput.wasHomeGesture();
    case XP_UI_WEB_WATCHDOG:
      resetTaskWatchdogIfSubscribed();
      return 0;
    case XP_UI_WEB_YIELD:
      yield();
      return 0;
    case XP_UI_WEB_HANDLE_CLIENT:
      self.webServer->handleClient();
      return 0;
    case XP_UI_WEB_READ_CANCEL:
      return self.cancelLatch;
    case XP_UI_WEB_LOG_GAP:
      LOG_DBG("WEBACT", "WARNING: %lu ms gap since last handleClient", static_cast<unsigned long>(value));
      return 0;
    case XP_UI_WEB_LOG_WIFI_LOST: {
      int count;
      {
        ModelLock lock(self);
        count = self.model.disconnects;
      }
      LOG_DBG("WEBACT", "WiFi not connected (status=%d, consecutive=%d, total=%lu ms)", call.wifiStatus, count,
              static_cast<unsigned long>(value));
      return 0;
    }
    case XP_UI_WEB_LOG_ABANDON:
      LOG_DBG("WEBACT", "WiFi unavailable for >%lu s; returning to network selection", 300UL);
      return 0;
    case XP_UI_WEB_LOG_RECOVERED: {
      int count;
      {
        ModelLock lock(self);
        count = self.model.disconnects;
      }
      LOG_DBG("WEBACT", "WiFi recovered after %d failed checks (%lu ms)", count, static_cast<unsigned long>(value));
      return 0;
    }
    case XP_UI_WEB_LOG_WEAK:
      LOG_DBG("WEBACT", "Warning: Weak WiFi signal: %d dBm", static_cast<int32_t>(value));
      return 0;
    case XP_UI_WEB_USB_ACTIVITY:
#if FREEINK_CAP_USB_MSC
      activityManager.goToUsbDrive();
#endif
      return 0;
    case XP_UI_WEB_LOG_WIFI_RESULT:
      LOG_DBG("WEBACT", "WifiSelectionActivity completed, connected=%d", value != 0);
      return 0;
    case XP_UI_WEB_USB_SUPPORTED:
      return FREEINK_CAP_USB_MSC;
    case XP_UI_WEB_AP_ERROR_LOG:
      LOG_ERR("WEBACT", "ERROR: Failed to start Access Point!");
      return 0;
    case XP_UI_WEB_CLEAR_CONNECTION: {
      ResourceLock lock(self);
      self.connectedIP.clear();
      self.connectedSSID.clear();
      return 0;
    }
  }
  assert(false && "Unknown web operation");
  return 0;
}
bool CrossPointWebServerActivity::rustSceneCurrent() const {
  return model.phase == renderingState.phase && model.mode == renderingState.mode &&
         model.is_ap == renderingState.is_ap;
}
bool CrossPointWebServerActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    renderingState = model;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const XpUiWebLayout layout{
      {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
      UI_10_FONT_ID,
      SMALL_FONT_ID,
      static_cast<int16_t>(m.topPadding),
      static_cast<int16_t>(m.headerHeight),
      static_cast<int16_t>(m.tabBarHeight),
      static_cast<int16_t>(m.verticalSpacing),
      static_cast<int16_t>(m.contentSidePadding),
      static_cast<int16_t>(renderer.getLineHeight(UI_10_FONT_ID)),
      static_cast<uint8_t>(renderingState.phase == XP_UI_WEB_RUNNING && !renderingState.is_ap &&
                           WiFi.status() == WL_CONNECTED),
      {0, 0, 0}};
  const XpUiBuffer buffer{commands, nullptr, 16, 0};
  return xp_ui_network_web_view(&renderingState, &layout, &buffer, &scene) == 1;
}
void CrossPointWebServerActivity::render(RenderLock&&) {
  bool painted = false;
  uint8_t refresh = XP_UI_NONE;
  {
    ResourceLock lock(*this);
    alternateUrl.reset();
    primaryUrl.reset();
    wifiConfig.reset();
    XpUiScene scene{};
    if (buildRustScene(scene)) {
      refresh = scene.refresh;
      scene.refresh = XP_UI_NONE;
      painted = paintRustScene(scene);
    } else {
      rejectRustScene();
    }
    alternateUrl.reset();
    primaryUrl.reset();
    wifiConfig.reset();
  }
  if (painted && refresh == XP_UI_FAST) renderer.displayBuffer();
}
XpUiPaint CrossPointWebServerActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void*, uint32_t handle, uint32_t generation) -> uint8_t {
            return generation == 1 && (handle == XP_UI_NETWORK_HEADER || handle == XP_UI_NETWORK_FOOTER ||
                                       (handle >= XP_UI_WEB_SUBHEADER && handle <= XP_UI_WEB_ALTERNATE_URL));
          },
          [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<CrossPointWebServerActivity*>(context);
            using networkui::Kind;
            networkui::Resource resource{Kind::Text};
            switch (command->resource) {
              case XP_UI_NETWORK_HEADER:
                resource = {Kind::Header, {self.renderingState.is_ap ? tr(STR_HOTSPOT_MODE) : tr(STR_FILE_TRANSFER)}};
                break;
              case XP_UI_NETWORK_FOOTER: {
                self.alternateUrl.reset();
                self.primaryUrl.reset();
                self.wifiConfig.reset();
                resource = {Kind::Footer, {tr(STR_EXIT), "", "", ""}};
                break;
              }
              case XP_UI_WEB_SUBHEADER:
                resource = {Kind::Subheader, {self.connectedSSID.c_str()}};
                break;
              case XP_UI_WEB_STARTING_TEXT:
                resource.labels[0] = tr(STR_STARTING_HOTSPOT);
                break;
              case XP_UI_WEB_CONNECT_HINT:
                resource.labels[0] = tr(STR_CONNECT_WIFI_HINT);
                break;
              case XP_UI_WEB_WIFI_QR:
                self.wifiConfig.emplace(std::string("WIFI:T:nopass;S:") + self.connectedSSID + ";;");
                resource = {Kind::Qr, {}, &*self.wifiConfig};
                break;
              case XP_UI_WEB_SSID_TEXT:
                resource.labels[0] = self.connectedSSID.c_str();
                break;
              case XP_UI_WEB_OPEN_HINT:
                resource.labels[0] = tr(STR_OPEN_URL_HINT);
                break;
              case XP_UI_WEB_SCAN_HINT:
                resource.labels[0] = tr(STR_SCAN_QR_HINT);
                break;
              case XP_UI_WEB_URL_QR:
                if (self.renderingState.is_ap) {
                  self.primaryUrl.emplace(std::string("http://") + AP_HOSTNAME + ".local/");
                  self.alternateUrl.emplace(tr(STR_OR_HTTP_PREFIX) + self.connectedIP + "/");
                } else
                  self.primaryUrl.emplace("http://" + self.connectedIP + "/");
                resource = {Kind::Qr, {}, &*self.primaryUrl};
                break;
              case XP_UI_WEB_PRIMARY_URL:
                resource.labels[0] = self.primaryUrl->c_str();
                break;
              case XP_UI_WEB_ALTERNATE_URL:
                if (!self.renderingState.is_ap)
                  self.alternateUrl.emplace(std::string(tr(STR_OR_HTTP_PREFIX)) + AP_HOSTNAME + ".local/");
                resource.labels[0] = self.alternateUrl->c_str();
                break;
              default:
                return;
            }
            networkui::paint(self.renderer, self.mappedInput, *command, clip, resource);
          }};
}
