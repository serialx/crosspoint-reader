#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "UsbDriveActivity.h"
#include "app_leaves.h"

#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
enum class UsbDriveState : uint8_t { Unsupported, WaitingForHost, Connected, Ejected, Disconnected, IoError };
bool navigationPending();
struct StorageMock {
  int begin = 0, end = 0, disconnect = 0;
  int beginWithPendingNavigation = 0;
  bool suspended = false;
  UsbDriveState phase = UsbDriveState::WaitingForHost;
  bool beginUsbDrive() {
    if (navigationPending()) ++beginWithPendingNavigation;
    return ++begin;
  }
  void endUsbDrive() { ++end; }
  UsbDriveState usbDriveState() { return phase; }
  bool usbDriveHostSuspended() { return suspended; }
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
  bool detected = true;
  bool present() { return detected; }
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
class FrontlightPanelActivity : public Activity {
 public:
  FrontlightPanelActivity(GfxRenderer& r, MappedInputManager& i) : Activity("FrontlightPanel", r, i) {}
};
class ActivityManager;
ActivityManager* manager;

bool send(XpAppEvent event) { return xp_app_event(&event) == XP_APP_ACCEPTED; }
class ActivityManager {
 public:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  XpAppLegacy legacy = appTestLegacy();
  XpAppResources resources = appTestResources();
  XpAppRuntime runtime = appTestRuntime();
  XpAppCompatibility compatibility{};
  enum class PendingAction { None, Push, Pop, Replace, ReplaceForSleep };
  ActivityManager(GfxRenderer& r, MappedInputManager& i) : renderer(r), mappedInput(i) {}
  void begin() {
    legacy.create = [](const XpAppLaunch* launch) -> void* {
      assert(launch->kind == XP_APP_FRONTLIGHT_PANEL);
      return new FrontlightPanelActivity(manager->renderer, manager->mappedInput);
    };
    legacy.destroy = [](void* object) { delete static_cast<Activity*>(object); };
    legacy.enter = [](void* object) { static_cast<Activity*>(object)->onEnter(); };
    legacy.loop = [](void* object) { static_cast<Activity*>(object)->loop(); };
    legacy.exit = [](void* object) { static_cast<Activity*>(object)->onExit(); };
    legacy.render = [](void* object, XpAppRenderToken* token) {
      static_cast<Activity*>(object)->render(std::move(*static_cast<RenderLock*>(token->lock)));
    };
    legacy.facts = [](const void* object, uint8_t delay, uint8_t input) -> XpAppLegacyFacts {
      auto* activity = const_cast<Activity*>(static_cast<const Activity*>(object));
      XpAppLegacyFacts facts{};
      facts.kind = activity->name == "FrontlightPanel" ? XP_APP_FRONTLIGHT_PANEL : XP_APP_USB_DRIVE;
      facts.exclusive_storage = activity->requiresExclusiveStorageLoop();
      facts.prevent_sleep = activity->preventAutoSleep();
      return facts;
    };
    legacy.take_result = [](void*, XpAppResultSink sink, void* context) -> uint8_t {
      XpAppResultView result{};
      return sink(context, &result);
    };
    legacy.home_gesture = [](void* object) -> uint8_t { return static_cast<Activity*>(object)->handleHomeGesture(); };
    runtime.input_mapped_snapshot = [](uint8_t, uint8_t full, uint8_t, XpAppMappedSnapshot* snapshot) {
      *snapshot = {};
      snapshot->home_gesture = manager->mappedInput.wasHomeGesture();
      snapshot->light_panel_gesture = manager->mappedInput.wasLightPanelGesture();
      snapshot->input.pressed = manager->mappedInput.pressed;
      snapshot->input.released = manager->mappedInput.released;
      snapshot->input.held = manager->mappedInput.held;
      if (!full) {
        snapshot->input.pressed &= XP_UI_POWER;
        snapshot->input.released &= XP_UI_POWER;
        snapshot->input.held &= XP_UI_POWER;
      }
      snapshot->input.now_ms = testNow;
    };
    runtime.now_ms = []() -> uint32_t { return testNow; };
    runtime.ble_before_screen_change = [](uint8_t) -> uint8_t { return 1; };
    runtime.render_lock = []() -> void* { return manager; };
    runtime.render_try_lock = runtime.render_lock;
    runtime.frontlight_present = []() -> uint8_t { return Frontlight.present(); };
    const XpAppPlatform platform{&legacy, &resources, &runtime};
    assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
  }
  XpAppFacts facts() const { return compatibility.activity_facts(); }
  std::string currentName() const {
    if (auto* activity = static_cast<Activity*>(xp_app_test_current_legacy())) return activity->name;
    assert(xp_app_test_is_home());
    return "Home";
  }
  PendingAction pendingAction() const { return static_cast<PendingAction>(xp_app_test_pending_kind()); }
  bool pendingOwner() const {
    const auto kind = pendingAction();
    return kind == PendingAction::Push || kind == PendingAction::Replace || kind == PendingAction::ReplaceForSleep;
  }
  void loop();
  void exitActivity(const RenderLock&) { legacy.exit(xp_app_test_current_legacy()); }
  void replaceActivity(std::unique_ptr<Activity>&&);
  void pushActivity(std::unique_ptr<Activity>&&);
  void popActivity();
  void goHome(HomeMenuItem = HomeMenuItem::NONE, bool = false);
  bool requiresExclusiveStorageLoop() const;
  void requestUpdate() {
    XpAppEvent event{.kind = XP_APP_RENDER_REQUESTED};
    send(event);
  }
  void paint() {
    RenderLock lock;
    XpAppRenderToken token{&lock};
    xp_app_render(&token);
  }
};
bool navigationPending() { return manager->pendingAction() != ActivityManager::PendingAction::None; }
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
  activityManager.begin();
  buildOkay = !buildFailure;
  renderer.acceptScene = buildFailure;
  activityManager.replaceActivity(std::make_unique<UsbDriveActivity>(renderer, input));
  assert(Storage.begin == 0 && Storage.end == 0 && restarts == 0);
  if (action.starts_with("active")) {
    // A successful later preparing frame permits Begin exactly once.
    activityManager.loop();
    buildOkay = renderer.acceptScene = true;
    activityManager.paint();
    activityManager.loop();
    assert(Storage.begin == 1 && activityManager.requiresExclusiveStorageLoop());
    activityManager.popActivity();
    for (int n = 0; n < 3; ++n) activityManager.loop();
    assert(activityManager.currentName() == "UsbDrive");
    assert(activityManager.pendingAction() == ActivityManager::PendingAction::Pop);
    assert(Storage.begin == 1 && Storage.end == 0 && restarts == 0);
    if (action == "active-io") {
      Storage.phase = UsbDriveState::IoError;
      activityManager.loop();
      assert(Storage.disconnect == 1 && Storage.end == 0 && restarts == 0);
      assert(activityManager.requiresExclusiveStorageLoop());
      testNow += 1000;
    } else if (action == "active-suspend") {
      Storage.phase = UsbDriveState::Connected;
      Storage.suspended = true;
      activityManager.loop();
      assert(Storage.end == 0 && restarts == 0 && activityManager.requiresExclusiveStorageLoop());
      testNow += 2000;
    } else if (action == "active-back") {
      input.pressed = 1u << unsigned(MappedInputManager::Button::Back);
    } else if (action == "active-power") {
      input.pressed = 1u << unsigned(MappedInputManager::Button::Power);
    } else if (action == "active-home") {
      input.home = true;
    } else {
      Storage.phase = UsbDriveState::Disconnected;
    }
    activityManager.loop();
    assert(Storage.end == 1 && restarts == 1);
    // Restart is terminal on hardware; a returning host restart must still not navigate.
    assert(activityManager.currentName() == "UsbDrive");
    assert(activityManager.pendingAction() == ActivityManager::PendingAction::Pop);
    assert(activityManager.requiresExclusiveStorageLoop());
    input.pressed = 0;
    input.home = false;
    activityManager.loop();
    assert(Storage.end == 1 && restarts == 1);
    assert(activityManager.currentName() == "UsbDrive");
    RenderLock lock;
    activityManager.exitActivity(lock);
    assert(Storage.end == 1);
  } else if (action.starts_with("panel-")) {
    activityManager.loop();  // Consume Rejected and request the retry frame.
    buildOkay = renderer.acceptScene = true;
    const bool before = action == "panel-before";
    if (before) activityManager.paint();
    input.lightPanel = true;
    activityManager.loop();
    input.lightPanel = false;  // One input frame, like the top-edge swipe.
    if (!before) activityManager.paint();
    activityManager.loop();
    assert(activityManager.currentName() == "UsbDrive");
    assert(xp_app_test_parent_count() == 0);
    assert(activityManager.pendingAction() == ActivityManager::PendingAction::None);
    assert(!activityManager.pendingOwner());
    assert(Storage.begin == 1 && Storage.beginWithPendingNavigation == 0);
    assert(Storage.end == 0 && restarts == 0 && activityManager.requiresExclusiveStorageLoop());
  } else {
    const bool recovered = action.find('-') != std::string::npos;
    const bool before = action.ends_with("-before");
    if (recovered) {
      activityManager.loop();  // Consume Rejected before delivering cancellation.
      buildOkay = renderer.acceptScene = true;
      if (before) activityManager.paint();
    }
    if (action.starts_with("back")) input.pressed = 1u << unsigned(MappedInputManager::Button::Back);
    if (action.starts_with("power")) input.pressed = 1u << unsigned(MappedInputManager::Button::Power);
    if (action.starts_with("home")) input.home = true;
    activityManager.loop();
    input.pressed = 0;
    input.home = false;
    if (recovered && !before) activityManager.paint();
    for (int n = 0; n < 2; ++n) activityManager.loop();
    assert(activityManager.currentName() == "Home");
    assert(activityManager.pendingAction() == ActivityManager::PendingAction::None);
    assert(!activityManager.pendingOwner());
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
