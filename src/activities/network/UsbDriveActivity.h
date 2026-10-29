#pragma once
#include <xp_ui_network.h>

#include "activities/RustActivityAdapter.h"

class UsbDriveActivity final : public RustActivityAdapter {
 public:
  UsbDriveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : RustActivityAdapter("UsbDrive", renderer, mappedInput) {
    model.preparing = 1;
  }
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return xp_ui_network_usb_prevent_sleep(&model); }
  bool requiresExclusiveStorageLoop() const override {
    ModelLock lock(*const_cast<UsbDriveActivity*>(this));
    // Only pre-handoff cancellation may release queued navigation without a restart.
    return model.preparing == 0 || model.restart_requested == 0;
  }

 private:
  XpUiUsbState model{}, renderingState{};
  XpUiCommand commands[6]{};
  enum class PreparingFrame : uint8_t { Pending, Displayed, Rejected };
  PreparingFrame preparingFrame = PreparingFrame::Pending;
  void rustSceneFailed() override { preparingFrame = PreparingFrame::Rejected; }
  void run(XpUiNetworkEvent event);
  static uint32_t service(void*, uint32_t operation, uint32_t value);
  static const char* text(uint32_t handle);
  bool buildRustScene(XpUiScene&) override;
  XpUiPaint rustResources() override;
};
