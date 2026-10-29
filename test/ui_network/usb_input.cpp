#include <xp_ui_network.h>

#include <cassert>
#include <future>

#include "RustActivityAdapter.h"
#define LOG_INF(...) ((void)0)
enum class UsbDriveState : uint8_t { Unsupported, WaitingForHost, Connected, Ejected, Disconnected, IoError };
struct StorageMock {
  int begin = 0, end = 0, disconnect = 0;
  UsbDriveState phase = UsbDriveState::WaitingForHost;
  bool suspended = false;
  bool beginUsbDrive() {
    ++begin;
    return true;
  }
  void endUsbDrive() { ++end; }
  UsbDriveState usbDriveState() { return phase; }
  bool usbDriveHostSuspended() { return suspended; }
  bool disconnectUsbDriveHost() {
    ++disconnect;
    return true;
  }
} Storage;
int restarts = 0;
void restartToHomeAfterStorageHandoff() { ++restarts; }
void delay(uint32_t n) { testNow += n; }
class UsbDriveActivity final : public RustActivityAdapter {
 public:
  XpUiUsbState model{}, renderingState{};
  enum class PreparingFrame : uint8_t { Pending, Displayed, Rejected };
  PreparingFrame preparingFrame = PreparingFrame::Pending;
  bool buildOkay = true;
  int requested = 0, finished = 0;
  UsbDriveActivity(GfxRenderer& r, MappedInputManager& i) : RustActivityAdapter("UsbDrive", r, i) {
    model.preparing = 1;
  }
  void finish() { ++finished; }
  void requestUpdate() { ++requested; }
  void requestUpdateAndWait() {
    RenderLock lock;
    render(std::move(lock));
  }
  void loop() override;
  void render(RenderLock&&) override;
  void run(XpUiNetworkEvent);
  static uint32_t service(void*, uint32_t, uint32_t);
  bool buildRustScene(XpUiScene& scene) override {
    ModelLock lock(*this);
    renderingState = model;
    scene.refresh = XP_UI_FAST;
    return buildOkay;
  }
  void rustSceneFailed() override;
};
// PRODUCTION_METHODS
int main() {
  for (bool buildFailure : {false, true}) {
    for (int cancel : {0, 1, 2, 3}) {
      Storage = {};
      restarts = 0;
      testNow = 0;
      GfxRenderer renderer;
      renderer.finishRefresh = true;
      MappedInputManager input;
      UsbDriveActivity activity(renderer, input);
      activity.buildOkay = !buildFailure;
      renderer.acceptScene = buildFailure;
      activity.run(XP_UI_NETWORK_ENTER);
      assert(Storage.begin == 0 && Storage.end == 0 && activity.model.preparing);
      activity.loop();
      assert(activity.requested == 1 && Storage.begin == 0);
      if (cancel) {
        if (cancel == 1) input.pressed = 1u << unsigned(MappedInputManager::Button::Back);
        if (cancel == 2) input.pressed = 1u << unsigned(MappedInputManager::Button::Power);
        if (cancel == 3) input.home = true;
        activity.loop();
        activity.run(XP_UI_NETWORK_EXIT);
        assert(activity.finished == 1 && Storage.begin == 0 && Storage.end == 0 && restarts == 0);
        continue;
      }
      activity.buildOkay = renderer.acceptScene = true;
      renderer.finishRefresh = false;
      auto render = std::async(std::launch::async, [&] {
        RenderLock lock;
        activity.render(std::move(lock));
      });
      while (!renderer.refreshing) std::this_thread::yield();
      activity.loop();
      assert(Storage.begin == 0);
      renderer.finishRefresh = true;
      render.get();
      activity.loop();
      assert(Storage.begin == 1 && !activity.model.preparing);
      activity.loop();
      assert(Storage.begin == 1);
      // Both rejection paths must leave active-session safety cleanup running.
      activity.buildOkay = !buildFailure;
      renderer.acceptScene = buildFailure;
      {
        RenderLock lock;
        activity.render(std::move(lock));
      }
      Storage.phase = UsbDriveState::IoError;
      activity.loop();
      assert(Storage.disconnect == 1);
      testNow += 1000;
      activity.loop();
      assert(Storage.end == 1 && restarts == 1 && Storage.begin == 1);
    }
    for (bool suspend : {false, true}) {
      Storage = {};
      restarts = 0;
      testNow = 100;
      GfxRenderer renderer;
      renderer.finishRefresh = true;
      MappedInputManager input;
      UsbDriveActivity activity(renderer, input);
      activity.run(XP_UI_NETWORK_ENTER);
      assert(Storage.begin == 1);
      activity.buildOkay = !buildFailure;
      renderer.acceptScene = buildFailure;
      {
        RenderLock lock;
        activity.render(std::move(lock));
      }
      Storage.phase = suspend ? UsbDriveState::Connected : UsbDriveState::Disconnected;
      Storage.suspended = suspend;
      activity.loop();
      if (suspend) {
        testNow += 2000;
        activity.loop();
      }
      assert(Storage.end == 1 && restarts == 1 && Storage.begin == 1);
    }
  }
}
