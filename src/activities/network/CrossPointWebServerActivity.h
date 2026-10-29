#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <xp_ui_network.h>

#include <memory>
#include <optional>
#include <string>

#include "NetworkModeSelectionActivity.h"
#include "activities/RustActivityAdapter.h"
#include "network/CrossPointWebServer.h"
class CrossPointWebServerActivity final : public RustActivityAdapter {
  XpUiWebState model{}, renderingState{};
  XpUiCommand commands[16]{};
  std::unique_ptr<CrossPointWebServer> webServer;
  std::string connectedIP, connectedSSID;
  std::optional<std::string> wifiConfig, primaryUrl, alternateUrl;
  uint8_t cancelLatch = 0;
  bool runningPolicy = false;
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateMutexStatic(&resourceMutexStorage);
  class ResourceLock {
    SemaphoreHandle_t mutex;

   public:
    explicit ResourceLock(CrossPointWebServerActivity& self) : mutex(self.resourceMutex) {
      xSemaphoreTake(mutex, portMAX_DELAY);
    }
    ~ResourceLock() { xSemaphoreGive(mutex); }
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
  };
  struct CallContext;
  void run(uint8_t event, uint8_t value = 0);
  static uint32_t service(void*, uint32_t, uint32_t);
  void onNetworkModeSelected(NetworkMode);
  void onWifiSelectionComplete(bool);
  bool buildRustScene(XpUiScene&) override;
  XpUiPaint rustResources() override;

 public:
  explicit CrossPointWebServerActivity(GfxRenderer& r, MappedInputManager& i, bool startInJoinNetwork = false)
      : RustActivityAdapter("CrossPointWebServer", r, i) {
    model.start_in_join = startInJoinNetwork;
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return webServer && webServer->isRunning(); }
  bool preventAutoSleep() override { return webServer && webServer->isRunning(); }
};
