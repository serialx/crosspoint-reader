#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <xp_ui_network.h>

#include <memory>
#include <optional>
#include <string>

#include "activities/RustActivityAdapter.h"
#include "network/CrossPointWebServer.h"
class CalibreConnectActivity final : public RustActivityAdapter {
  XpUiCalibreState model{}, renderingState{};
  XpUiCommand commands[14]{};
  std::unique_ptr<CrossPointWebServer> webServer;
  std::string connectedIP, connectedSSID, currentUploadName, lastCompleteName;
  // Keep the original label lifetime through progress-bar painting.
  std::optional<std::string> paintingUploadLabel;
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateMutexStatic(&resourceMutexStorage);
  class ResourceLock {
    SemaphoreHandle_t mutex;

   public:
    explicit ResourceLock(CalibreConnectActivity& self) : mutex(self.resourceMutex) {
      xSemaphoreTake(mutex, portMAX_DELAY);
    }
    ~ResourceLock() { xSemaphoreGive(mutex); }
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
  };
  struct CallContext {
    CalibreConnectActivity& activity;
    std::optional<CrossPointWebServer::WsUploadStatus> upload;
  };
  void run(XpUiNetworkEvent, bool value = false);
  static uint32_t service(void*, uint32_t, uint32_t);
  static void publish(void*, const XpUiCalibreState*, uint32_t);
  static void upload(void*, XpUiCalibreUpload*);
  static const char* text(uint32_t);
  void onWifiSelectionComplete(bool connected);
  bool buildRustScene(XpUiScene&) override;
  XpUiPaint rustResources() override;

 public:
  explicit CalibreConnectActivity(GfxRenderer& renderer, MappedInputManager& input)
      : RustActivityAdapter("CalibreConnect", renderer, input) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
