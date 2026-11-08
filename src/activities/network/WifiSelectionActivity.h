#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <xp_network_owner.h>
#include <xp_ui_network.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "WifiCredentialStore.h"
#include "activities/RustActivityAdapter.h"

struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  bool isEncrypted;
  bool hasSavedPassword;
  bool isHiddenPlaceholder = false;
};
class WifiSelectionActivity final : public RustActivityAdapter {
  friend struct WifiPhysical;
  XpUiWifiState model{}, renderingState{};
  XpUiListState next{};
  XpUiWifiHits hits{}, nextHits{};
  bool routingReady = false;
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t commandCapacity = 0;
  // Explicit lifetimes let native resources end before the command buffer.
  union {
    freeink::ui::GfxRendererTarget textTarget;
  };
  union {
    std::vector<WifiNetworkInfo> networks;
  };
  union {
    std::vector<std::string> networkStatuses;
  };
  union {
    std::vector<std::string> autoAttemptedSsids;
  };
  union {
    std::string selectedSSID;
  };
  union {
    std::string connectedIP;
  };
  union {
    std::string connectionError;
  };
  union {
    std::string enteredPassword;
  };
  union {
    std::string cachedMacAddress;
  };
  union {
    std::optional<std::string> promptLabel;
  };
  union {
    std::optional<std::string> paintingSsid;
  };
  union {
    std::optional<std::string> paintingIp;
  };
  char renderingMac[64]{};
  struct VisibleRow {
    char ssid[33]{};
    char status[9]{};
    uint16_t index = 0;
    bool hidden = false;
  };
  std::unique_ptr<VisibleRow[]> visibleRows;
  uint16_t rowCapacity = 0;
  bool paintingRows = false, retireRequested = false;
  void retireRows();
  bool resourcesLive = true;
  void releaseResources();
  const bool allowAutoConnect;
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateMutexStatic(&resourceMutexStorage);
  class ResourceLock {
    SemaphoreHandle_t mutex;

   public:
    explicit ResourceLock(WifiSelectionActivity& self) : mutex(self.resourceMutex) {
      xSemaphoreTake(mutex, portMAX_DELAY);
    }
    ~ResourceLock() { release(); }
    void release() {
      if (mutex) {
        xSemaphoreGive(mutex);
        mutex = nullptr;
      }
    }
    ResourceLock(const ResourceLock&) = delete;
    ResourceLock& operator=(const ResourceLock&) = delete;
  };
  struct CallContext {
    WifiSelectionActivity& activity;
    std::optional<std::string> lastSsid;
    std::optional<WifiCredential> credential;
    char scanSsid[33]{};
    int32_t scanRssi = 0;
    uint32_t scanIndex = 0;
    uint32_t navigationRevision = 0;
    bool inputAction = false, inputRejected = false, committed = false;
    bool inputCurrent();
  };
  void run(XpUiWifiEvent, uint32_t value = 0, const XpUiInput* input = nullptr);
  static uint32_t service(void*, uint32_t, uint32_t);
  static void publish(void*, XpUiWifiState*);
  void keyboard(bool);
  void appendHiddenNetworkEntry();
  void rebuildNetworkStatuses();
  static std::string getSignalStrengthIndicator(int32_t);
  void onComplete(bool);
  const char* text(uint32_t) const;
  XpUiWifiViewServices resources();
  bool buildRustScene(XpUiScene&) override;
  bool rustSceneCurrent() const override;
  XpUiPaint rustResources() override;
  void rustPublished() override;
  void rustSceneFailed() override;

 public:
  WifiSelectionActivity(GfxRenderer&, MappedInputManager&, bool autoConnect = true);
  ~WifiSelectionActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
