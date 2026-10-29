#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "UsbDriveActivity.h"

#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
enum class UsbDriveState : uint8_t { Unsupported, WaitingForHost, Connected, Ejected, Disconnected, IoError };
struct StorageMock {
  int begin = 0, end = 0, disconnect = 0;
  UsbDriveState phase = UsbDriveState::WaitingForHost;
  bool beginUsbDrive() { return ++begin; }
  void endUsbDrive() { ++end; }
  UsbDriveState usbDriveState() { return phase; }
  bool usbDriveHostSuspended() { return false; }
  bool disconnectUsbDriveHost() { return ++disconnect; }
} Storage;
int restarts = 0;
void restartToHomeAfterStorageHandoff() { ++restarts; }
void delay(uint32_t n) { testNow += n; }
bool buildOkay = true;
bool UsbDriveActivity::buildRustScene(XpUiScene& scene) {
  ModelLock lock(*this);
  renderingState = model;
  scene.refresh = XP_UI_FAST;
  return buildOkay;
}
XpUiPaint UsbDriveActivity::rustResources() { return {}; }

// Platform leaves only; navigation and the exclusive decision are production methods.
struct HeaderBackTapTarget {
  static bool contains(int, int) { return false; }
  static void clear() {}
};
struct FrontlightMock {
  bool present() { return false; }
} Frontlight;
namespace haptic_feedback {
void touchAction() {}
}  // namespace haptic_feedback
namespace bleturner {
bool beforeScreenChange(int, bool) { return true; }
}  // namespace bleturner
constexpr int eIncrement = 0;
void xTaskNotify(void*, int, int) {}
enum class HomeMenuItem { NONE, FILE_BROWSER, LIBRARY, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU };
class HomeActivity : public Activity {
 public:
  HomeActivity(GfxRenderer& r, MappedInputManager& i, HomeMenuItem, bool) : Activity("Home", r, i) {}
  bool isHomeActivity() const override { return true; }
};
class FrontlightPanelActivity : public Activity {
 public:
  FrontlightPanelActivity(GfxRenderer& r, MappedInputManager& i) : Activity("FrontlightPanel", r, i) {}
};
class ActivityManager {
 public:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity, pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace, ReplaceForSleep };
  PendingAction pendingAction = PendingAction::None;
  std::atomic<bool> requestedUpdate{false};
  void* renderTaskHandle = nullptr;
  uint32_t screenVisit = 0;
  ActivityManager(GfxRenderer& r, MappedInputManager& i) : renderer(r), mappedInput(i) {}
  void loop();
  void exitActivity(const RenderLock&);
  void replaceActivity(std::unique_ptr<Activity>&&);
  void pushActivity(std::unique_ptr<Activity>&&);
  void popActivity();
  void goHome(HomeMenuItem = HomeMenuItem::NONE, bool = false);
  bool requiresExclusiveStorageLoop() const;
  void requestUpdate() { requestedUpdate = true; }
  void paint() {
    RenderLock lock;
    if (currentActivity) currentActivity->render(std::move(lock));
  }
};
ActivityManager* manager;
void Activity::finish() { manager->popActivity(); }
void Activity::requestUpdate() { manager->requestUpdate(); }
void Activity::requestUpdateAndWait() { manager->paint(); }

// PRODUCTION_METHODS

int main(int argc, char** argv) {
  assert(argc == 3);
  const bool buildFailure = std::string(argv[1]) == "build";
  const std::string action = argv[2];
  GfxRenderer renderer;
  renderer.finishRefresh = true;
  MappedInputManager input;
  ActivityManager activityManager(renderer, input);
  manager = &activityManager;
  buildOkay = !buildFailure;
  renderer.acceptScene = buildFailure;
  activityManager.replaceActivity(std::make_unique<UsbDriveActivity>(renderer, input));
  assert(Storage.begin == 0 && Storage.end == 0 && restarts == 0);
  if (action == "active") {
    // A successful later preparing frame permits Begin exactly once.
    activityManager.loop();
    buildOkay = renderer.acceptScene = true;
    activityManager.paint();
    activityManager.loop();
    assert(Storage.begin == 1 && activityManager.requiresExclusiveStorageLoop());
    activityManager.popActivity();
    for (int n = 0; n < 3; ++n) activityManager.loop();
    assert(activityManager.currentActivity->name == "UsbDrive");
    assert(activityManager.pendingAction == ActivityManager::PendingAction::Pop);
    assert(Storage.begin == 1 && Storage.end == 0 && restarts == 0);
    Storage.phase = UsbDriveState::Disconnected;
    activityManager.loop();
    assert(Storage.end == 1 && restarts == 1);
    // Restart is terminal on hardware; a returning host restart must still not navigate.
    assert(activityManager.currentActivity->name == "UsbDrive");
    assert(activityManager.pendingAction == ActivityManager::PendingAction::Pop);
    RenderLock lock;
    activityManager.exitActivity(lock);
    assert(Storage.end == 1);
  } else {
    if (action == "back") input.pressed = 1u << unsigned(MappedInputManager::Button::Back);
    if (action == "power") input.pressed = 1u << unsigned(MappedInputManager::Button::Power);
    if (action == "home") input.home = true;
    // Home queues Replace in the manager; Back/Power queue Pop in the USB policy.
    for (int n = 0; n < 3; ++n) activityManager.loop();
    assert(activityManager.currentActivity->name == "Home");
    assert(activityManager.pendingAction == ActivityManager::PendingAction::None);
    assert(!activityManager.pendingActivity);
    assert(!activityManager.requiresExclusiveStorageLoop());
    assert(Storage.begin == 0 && Storage.end == 0 && restarts == 0);
    buildOkay = renderer.acceptScene = true;
    input.pressed = 0;
    input.home = false;
    activityManager.paint();
    activityManager.loop();
    assert(Storage.begin == 0 && Storage.end == 0 && restarts == 0);
  }
  std::printf("%s/%s passed\n", argv[1], argv[2]);
}
