#include "CalibreConnectActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "NetworkUiBinding.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"
namespace {
constexpr const char* HOSTNAME = "crosspoint";
}
static_assert(sizeof(XpUiCalibreState) == 24 && alignof(XpUiCalibreState) == 4);
static_assert(sizeof(XpUiCalibreUpload) == 16 && alignof(XpUiCalibreUpload) == 4);
static_assert(sizeof(XpUiCalibreLayout) == 40 && alignof(XpUiCalibreLayout) == 4);
static_assert(sizeof(XpUiCalibreServices) == 4 * sizeof(void*));
void CalibreConnectActivity::onEnter() {
  Activity::onEnter();
  run(XP_UI_NETWORK_ENTER);
}
void CalibreConnectActivity::onExit() {
  Activity::onExit();
  run(XP_UI_NETWORK_EXIT);
}
void CalibreConnectActivity::onWifiSelectionComplete(bool connected) { run(XP_UI_NETWORK_CHILD_RESULT, connected); }
void CalibreConnectActivity::loop() {
  consumeRejectedInput();
  // Transport progress and the existing cancel policy continue during rejection.
  run(XP_UI_NETWORK_TICK);
}
void CalibreConnectActivity::run(XpUiNetworkEvent event, bool value) {
  XpUiCalibreState next;
  {
    ModelLock lock(*this);
    next = model;
  }
  XpUiInput input{};
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) input.pressed = XP_UI_BACK;
  CallContext context{*this, {}};
  const XpUiCalibreServices services{&context, service, publish, upload};
  xp_ui_network_calibre_run(&next, &services, event, &input, value);
}
uint32_t CalibreConnectActivity::service(void* context, uint32_t operation, uint32_t value) {
  auto& call = *static_cast<CallContext*>(context);
  auto& self = call.activity;
  switch (static_cast<XpUiCalibreCall>(operation)) {
    case XP_UI_CALIBRE_NOW:
      return millis();
    case XP_UI_CALIBRE_PAINT:
      self.requestUpdate();
      break;
    case XP_UI_CALIBRE_WIFI_CONNECTED:
      return WiFi.status() == WL_CONNECTED;
    case XP_UI_CALIBRE_WIFI_PRESENT:
      return WiFi.getMode() != WIFI_MODE_NULL;
    case XP_UI_CALIBRE_WIFI_DISCONNECT:
      WiFi.disconnect(false);
      break;
    case XP_UI_CALIBRE_DELAY:
      delay(value);
      break;
    case XP_UI_CALIBRE_RESTART:
      silentRestart();
      break;
    case XP_UI_CALIBRE_MDNS_END:
      MDNS.end();
      break;
    case XP_UI_CALIBRE_MDNS_BEGIN:
      if (MDNS.begin(HOSTNAME)) LOG_DBG("CAL", "mDNS started: http://%s.local/", HOSTNAME);
      break;
    case XP_UI_CALIBRE_CACHE_RELEASE:
      if (auto* fcm = self.renderer.getFontCacheManager()) {
        LOG_DBG("CAL", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
        fcm->releaseSdFontCaches();
        LOG_DBG("CAL", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());
      }
      break;
    case XP_UI_CALIBRE_SERVER_BEGIN:
      self.webServer.reset(new CrossPointWebServer());
      self.webServer->begin();
      break;
    case XP_UI_CALIBRE_SERVER_RUNNING:
      return self.webServer && self.webServer->isRunning();
    case XP_UI_CALIBRE_HANDLE_CLIENT:
      self.webServer->handleClient();
      break;
    case XP_UI_CALIBRE_WATCHDOG:
      resetTaskWatchdogIfSubscribed();
      break;
    case XP_UI_CALIBRE_YIELD:
      yield();
      break;
    case XP_UI_CALIBRE_BACK:
      return self.mappedInput.wasPressed(MappedInputManager::Button::Back);
    case XP_UI_CALIBRE_DROP_UPLOAD:
      call.upload.reset();
      break;
    case XP_UI_CALIBRE_FINISH:
      self.finish();
      break;
    case XP_UI_CALIBRE_CONNECTED_DETAILS: {
      ResourceLock lock(self);
      self.connectedIP = WiFi.localIP().toString().c_str();
      self.connectedSSID = WiFi.SSID().c_str();
      break;
    }
    case XP_UI_CALIBRE_SELECT_WIFI:
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
      break;
    case XP_UI_CALIBRE_LOG_GAP:
      LOG_DBG("CAL", "WARNING: %lu ms gap since last handleClient", static_cast<unsigned long>(value));
      break;
  }
  return 0;
}
void CalibreConnectActivity::publish(void* context, const XpUiCalibreState* state, uint32_t changes) {
  auto& call = *static_cast<CallContext*>(context);
  auto& self = call.activity;
  if (changes) {
    ResourceLock resourceLock(self);
    if (changes & XP_UI_CALIBRE_CLEAR_ALL) {
      self.connectedIP.clear();
      self.connectedSSID.clear();
      self.currentUploadName.clear();
      self.lastCompleteName.clear();
    }
    if (changes & XP_UI_CALIBRE_COPY_UPLOAD) self.currentUploadName = call.upload->filename;
    if (changes & XP_UI_CALIBRE_CLEAR_UPLOAD) self.currentUploadName.clear();
    if (changes & XP_UI_CALIBRE_COPY_COMPLETED) self.lastCompleteName = call.upload->lastCompleteName;
    if (changes & XP_UI_CALIBRE_CLEAR_COMPLETED) self.lastCompleteName.clear();
    ModelLock modelLock(self);
    self.model = *state;
  } else {
    ModelLock lock(self);
    self.model = *state;
  }
}
void CalibreConnectActivity::upload(void* context, XpUiCalibreUpload* out) {
  auto& call = *static_cast<CallContext*>(context);
  auto& self = call.activity;
  call.upload.emplace(self.webServer->getWsUploadStatus());
  const auto& status = *call.upload;
  *out = {static_cast<uint32_t>(status.received),
          static_cast<uint32_t>(status.total),
          static_cast<uint32_t>(status.lastCompleteAt),
          static_cast<uint8_t>(status.inProgress),
          static_cast<uint8_t>(status.filename != self.currentUploadName),
          {0, 0}};
}
const char* CalibreConnectActivity::text(uint32_t handle) {
  switch (handle) {
    case XP_UI_CALIBRE_STARTING_TEXT:
      return tr(STR_CALIBRE_STARTING);
    case XP_UI_CALIBRE_ERROR_TEXT:
      return tr(STR_CONNECTION_FAILED);
    case XP_UI_CALIBRE_SETUP_TEXT:
      return tr(STR_CALIBRE_SETUP);
    case XP_UI_CALIBRE_INSTRUCTION_1:
      return tr(STR_CALIBRE_INSTRUCTION_1);
    case XP_UI_CALIBRE_INSTRUCTION_2:
      return tr(STR_CALIBRE_INSTRUCTION_2);
    case XP_UI_CALIBRE_INSTRUCTION_3:
      return tr(STR_CALIBRE_INSTRUCTION_3);
    case XP_UI_CALIBRE_INSTRUCTION_4:
      return tr(STR_CALIBRE_INSTRUCTION_4);
    case XP_UI_CALIBRE_STATUS_TEXT:
      return tr(STR_CALIBRE_STATUS);
    default:
      return nullptr;
  }
}
bool CalibreConnectActivity::rustSceneCurrent() const { return model.phase == renderingState.phase; }
bool CalibreConnectActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    renderingState = model;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  int width = 0;
  if (renderingState.phase == XP_UI_CALIBRE_STARTING)
    width = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_CALIBRE_STARTING));
  else if (renderingState.phase == XP_UI_CALIBRE_ERROR)
    width = renderer.getTextWidth(UI_12_FONT_ID, tr(STR_CONNECTION_FAILED), EpdFontFamily::BOLD);
  const XpUiCalibreLayout layout{
      {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())},
      UI_10_FONT_ID,
      UI_12_FONT_ID,
      SMALL_FONT_ID,
      static_cast<int16_t>(m.topPadding),
      static_cast<int16_t>(m.headerHeight),
      static_cast<int16_t>(m.tabBarHeight),
      static_cast<int16_t>(m.verticalSpacing),
      static_cast<int16_t>(m.contentSidePadding),
      static_cast<int16_t>(m.progressBarHeight),
      static_cast<int16_t>(renderer.getLineHeight(UI_10_FONT_ID)),
      static_cast<int16_t>(renderingState.phase == XP_UI_CALIBRE_RUNNING ? renderer.getTextHeight(UI_12_FONT_ID) : 0),
      static_cast<int16_t>(width),
      0};
  const XpUiBuffer buffer{commands, nullptr, 14, 0};
  const auto now = renderingState.phase == XP_UI_CALIBRE_RUNNING && renderingState.completed_at > 0 ? millis() : 0;
  return xp_ui_network_calibre_view(&renderingState, &layout, now, &buffer, &scene) == 1;
}
void CalibreConnectActivity::render(RenderLock&&) {
  bool painted = false;
  {
    ResourceLock lock(*this);
    paintingUploadLabel.reset();
    XpUiScene scene{};
    if (buildRustScene(scene)) {
      scene.refresh = XP_UI_NONE;
      painted = paintRustScene(scene);
    } else {
      rejectRustScene();
    }
    paintingUploadLabel.reset();
  }
  if (painted) renderer.displayBuffer();
}
XpUiPaint CalibreConnectActivity::rustResources() {
  return {
      this, nullptr, nullptr,
      [](void*, uint32_t handle, uint32_t generation) -> uint8_t {
        return generation == 1 && (handle == XP_UI_NETWORK_HEADER || handle == XP_UI_NETWORK_FOOTER ||
                                   (handle >= XP_UI_CALIBRE_SUBHEADER && handle <= XP_UI_CALIBRE_COMPLETED_TEXT));
      },
      [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
        auto& self = *static_cast<CalibreConnectActivity*>(context);
        const auto& r = command->rect;
        using networkui::Kind;
        networkui::Resource resource{Kind::Text};
        switch (command->resource) {
          case XP_UI_NETWORK_HEADER:
            resource = {Kind::Header, {tr(STR_CALIBRE_WIRELESS)}};
            break;
          case XP_UI_NETWORK_FOOTER: {
            resource = {Kind::Footer, {tr(STR_EXIT), "", "", ""}};
            break;
          }
          case XP_UI_CALIBRE_SUBHEADER: {
            const auto label = std::string(tr(STR_IP_ADDRESS_PREFIX)) + self.connectedIP;
            networkui::paint(self.renderer, self.mappedInput, *command, clip,
                             {Kind::Subheader, {self.connectedSSID.c_str(), label.c_str()}});
            return;
          }
          case XP_UI_CALIBRE_PROGRESS:
            networkui::paint(self.renderer, self.mappedInput, *command, clip,
                             {Kind::Progress, {}, nullptr, self.renderingState.received, self.renderingState.total});
            self.paintingUploadLabel.reset();
            return;
          case XP_UI_CALIBRE_RECEIVING_TEXT: {
            self.paintingUploadLabel.emplace(tr(STR_CALIBRE_RECEIVING));
            auto& label = *self.paintingUploadLabel;
            if (!self.currentUploadName.empty()) {
              label += ": " + self.currentUploadName;
              label = self.renderer.truncatedText(SMALL_FONT_ID, label.c_str(), r.w - r.x * 2, EpdFontFamily::REGULAR);
            }
            resource.labels[0] = label.c_str();
            break;
          }
          case XP_UI_CALIBRE_COMPLETED_TEXT: {
            std::string msg = std::string(tr(STR_CALIBRE_RECEIVED)) + self.lastCompleteName;
            msg = self.renderer.truncatedText(SMALL_FONT_ID, msg.c_str(), r.w - r.x * 2, EpdFontFamily::REGULAR);
            networkui::paint(self.renderer, self.mappedInput, *command, clip, {Kind::Text, {msg.c_str()}});
            return;
          }
          default:
            resource.labels[0] = text(command->resource);
            break;
        }
        networkui::paint(self.renderer, self.mappedInput, *command, clip, resource);
      }};
}
