#pragma once
#include <FreeInkUIGfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "activities/Activity.h"

class WifiSelectionActivity final : public Activity {
  friend struct WifiPhysical;
  // Rust releases this target before its command buffer, then destroys the wrapper.
  union {
    freeink::ui::GfxRendererTarget textTarget;
  };
  bool resourcesLive = true;
  void releaseResources();
  const bool allowAutoConnect;
  StaticSemaphore_t resourceMutexStorage{};
  SemaphoreHandle_t resourceMutex = xSemaphoreCreateMutexStatic(&resourceMutexStorage);

 public:
  WifiSelectionActivity(GfxRenderer&, MappedInputManager&, bool autoConnect = true);
  ~WifiSelectionActivity() override;
  bool preventAutoSleep() override { return true; }
};
