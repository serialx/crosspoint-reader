"""Exercise the production snapshot leaf and its ordered field consumers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/main.cpp").read_text()
start = source.index("void mappedSnapshot(")
body = source[start:source.index("\nconst XpAppRuntime& appRuntime()", start)]
manager = (ROOT / "src/activities/ActivityManager.cpp").read_text()
facts = manager.split(".facts = ", 1)[1].split(",\n      .take_result", 1)[0]
helpers = 'struct OwnedLegacyActivity' + manager.split('struct OwnedLegacyActivity', 1)[1].split('XpAppText text', 1)[0]
settings = source.split('.loop_settings =', 1)[1].split(',\n      .display_geometry', 1)[0]
harness = r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <xp_app.h>
void mappedSnapshot(uint8_t, uint8_t, uint8_t, XpAppMappedSnapshot*);
XpAppMappedSnapshot eventFrame{};
unsigned updates = 0, mappedReads = 0, touchReads = 0, keyReads = 0, releases = 0;
uint32_t millis() { return 100; }
struct HalGPIO {
  enum { BTN_POWER, BTN_DOWN };
  bool wasReleased(int) { return true; }
  bool wasPressed(int) { return true; }
  bool isPressed(int) { return true; }
  uint32_t getPowerButtonHeldTime() { return 700; }
  bool wasAnyPressed() { return true; }
  bool wasAnyReleased() { return true; }
  bool wasTouchActivity() { return true; }
  bool wasHomeKeyPressed() { ++keyReads; return true; }
  bool wasHomeKeyTapped() { ++keyReads; return true; }
  bool wasHomeKeyLongPressed() { ++keyReads; return true; }
  bool hasHomeKey() { ++keyReads; return true; }
  bool hasToggleSwitch() { return true; }
  bool isToggleSwitchOn() { return true; }
  bool wasUsbStateChanged() { return true; }
} gpio;
struct MappedInputManager {
  enum class Button { Back, Confirm, NavNext, NavPrevious, Left, Right, Up, Down, Power,
                      PageBack, PageForward, ScreenLeft, ScreenRight, ScreenUp, ScreenDown };
  enum class SwipeDir { None, Up, Down, Left, Right };
  bool injected = false;
  void update(bool defer) {
    assert(!defer);
    ++updates;
    mappedSnapshot(0, 0, 0, &eventFrame);
  }
  bool wasPressed(Button) { ++mappedReads; return injected; }
  bool wasReleased(Button b) { ++mappedReads; return injected || b == Button::Power; }
  bool isPressed(Button) { ++mappedReads; return injected; }
  uint32_t getHeldTime() { return 800; }
  SwipeDir wasSwipe() { return SwipeDir::Down; }
  bool consumeSuppressedRelease() { ++releases; return false; }
  bool wasHomeGesture() { return true; }
  bool wasLightPanelGesture() { return true; }
  bool hasTouch() { return true; }
  bool wasScreenTapped(int& x, int& y) { x = 30; y = 20; return true; }
} mappedInputManager;
struct Touch { int touchX=17, touchY=23; bool touchPressed=true, touchReleased=true, touchHeld=true; };
Touch touchSnapshotFrom(const MappedInputManager&) { ++touchReads; return {}; }
struct HeaderBackTapTarget { static bool contains(int,int) { return false; } };
namespace pageturner { std::atomic<bool> holdsCpu{true}; }
bool failOwner = false;
unsigned destroyed = 0, settingsLookups = 0;
std::vector<unsigned> reads;
struct SettingField {
  unsigned index;
  operator uint8_t() const { reads.push_back(index); return index; }
};
struct Settings {
  uint32_t getPowerButtonDuration() const { reads.push_back(1); return 700; }
  uint32_t getSleepTimeoutMs() const { reads.push_back(2); return 1200; }
  SettingField homeButtonTapAction{3}, homeButtonDoubleTapAction{4}, homeButtonLongPressAction{5},
    shortPwrBtn{6}, doubleClickPwrLight{7}, slideSwitchAction{8}, tiltPageTurn{9}, orientation{10},
    fadingFix{11}, screenInverted{12}, touchReaderControls{13}, frontlightOn{14};
} storedSettings;
Settings& settingsInstance() { ++settingsLookups; return storedSettings; }
#define SETTINGS settingsInstance()
#define FREEINK_DEVICE_PAPERMONO 0
struct HalPowerManager { static constexpr uint32_t IDLE_POWER_SAVING_MS = 100; };
struct BoardConfig { static bool isX4Pro() { return false; } };
#define LOG_ERR(...) ((void)0)
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (failOwner) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct Activity {
  std::string name = "EpubReader";
  unsigned delayCalls = 0;
  mutable unsigned identityCalls = 0;
  bool remote = true, prevent = false, exclusive = false;
  virtual ~Activity() { ++destroyed; }
  virtual bool isReaderActivity() const { ++identityCalls; return false; }
  bool acceptsRemoteInput() const { return remote; }
  bool preventAutoSleep() const { return prevent; }
  bool requiresExclusiveStorageLoop() const { return exclusive; }
  bool skipLoopDelay() { ++delayCalls; return true; }
};
struct ReaderActivity : Activity {
  unsigned overlayCalls = 0;
  bool isReaderActivity() const override { ++identityCalls; return true; }
  bool inputOverPage() { ++overlayCalls; return true; }
};
''' + helpers + '\nauto loopSettings = ' + settings + ';\n' + body + "\nauto legacyFacts = " + facts + ";\n" + r'''
int main() {
  auto readerOwner = ownLegacy(std::make_unique<ReaderActivity>());
  auto& reader = *static_cast<ReaderActivity*>(nativeActivity(readerOwner.get()));
  const auto summary = legacyFacts(readerOwner.get(), 0, 0);
  assert(summary.reader && summary.remote_input && !summary.skip_delay && !summary.input_over_page);
  assert(reader.delayCalls == 0 && reader.overlayCalls == 0);
  const auto delay = legacyFacts(readerOwner.get(), 1, 0);
  assert(delay.skip_delay && !delay.input_over_page && reader.delayCalls == 1 && reader.overlayCalls == 0);
  const auto overlay = legacyFacts(readerOwner.get(), 0, 1);
  assert(!overlay.skip_delay && overlay.input_over_page && reader.delayCalls == 1 && reader.overlayCalls == 1);
  auto settingsOwner = ownLegacy(std::make_unique<Activity>());
  auto& settings = *nativeActivity(settingsOwner.get());
  settings.name = "Settings";
  const auto native = legacyFacts(settingsOwner.get(), 1, 1);
  assert(native.kind == XP_APP_SETTINGS && !native.reader && native.skip_delay && !native.input_over_page);

  assert(reader.identityCalls == 1 && settings.identityCalls == 1);
  reader.name = "Settings";
  reader.remote = false;
  reader.prevent = reader.exclusive = true;
  const auto changed = legacyFacts(readerOwner.get(), 0, 0);
  assert(changed.kind == UINT8_MAX && changed.reader && !changed.remote_input);
  assert(changed.prevent_sleep && changed.exclusive_storage && reader.identityCalls == 1);
  settings.name = "FileBrowser";
  assert(legacyFacts(settingsOwner.get(), 0, 0).kind == XP_APP_SETTINGS);
  assert(settings.identityCalls == 1);
  readerOwner.reset();
  settingsOwner.reset();
  assert(destroyed == 2);
  // Reusing the same owner storage must not preserve the previous identity.
  alignas(OwnedLegacyActivity) unsigned char storage[sizeof(OwnedLegacyActivity)];
  auto* reused = new (storage) OwnedLegacyActivity(std::make_unique<Activity>());
  reused->activity->name = "FileBrowser";
  assert(legacyFacts(reused, 0, 0).kind == XP_APP_FILES);
  reused->~OwnedLegacyActivity();
  reused = new (storage) OwnedLegacyActivity(std::make_unique<ReaderActivity>());
  assert(legacyFacts(reused, 0, 0).reader && legacyFacts(reused, 0, 0).kind == UINT8_MAX);
  assert(reused->activity->identityCalls == 1);
  reused->~OwnedLegacyActivity();
  failOwner = true;
  const auto failed = ownLegacy(std::make_unique<Activity>());
  assert(!failed && destroyed == 5);
  assert(!ownLegacy(nullptr) && destroyed == 5);
  XpAppSettings config;
  for (unsigned n = 1; n <= 2; ++n) {
    reads.clear();
    storedSettings.homeButtonTapAction.index = 3 + n;
    loopSettings(&config);
    assert(settingsLookups == n && reads.size() == 14);
    for (unsigned i = 0; i < 14; ++i) assert(reads[i] == (i == 2 ? 3 + n : i + 1));
    assert(config.home_tap == 3 + n && config.power_duration == 700 && config.sleep_timeout == 1200);
  }

  XpAppMappedSnapshot driver;
  std::memset(&driver, 0xa5, sizeof(driver));
  mappedSnapshot(1, 0, 0, &driver);
  assert(updates == 1 && mappedReads == 1 && touchReads == 0 && keyReads == 4 && releases == 0);
  assert(eventFrame.input.now_ms == 100 && eventFrame.input.swipe == XP_UI_SWIPE_DOWN);
  assert(eventFrame.home_key_pressed && eventFrame.home_key_tapped && eventFrame.home_key_long_pressed);
  assert(eventFrame.has_home_key && eventFrame.power_released && eventFrame.cpu_hold);
  assert(!eventFrame.input.pressed && !eventFrame.input.released && !eventFrame.input.held);
  assert(!eventFrame.power_held && !eventFrame.has_switch && !eventFrame.home_gesture);
  assert(driver.input.released == XP_UI_POWER && !driver.input.pressed && !driver.input.held);
  assert(driver.power_held_ms == 700 && !driver.power_pressed && driver.power_released && driver.power_held);
  assert(driver.physical_down && driver.any_pressed && driver.any_released && driver.touch_activity);
  assert(driver.has_switch && driver.switch_on && driver.usb_changed && driver.cpu_hold);
  assert(!driver.home_key_pressed && !driver.home_key_tapped && !driver.home_key_long_pressed && !driver.has_home_key);
  assert(!driver.home_gesture && !driver.light_panel_gesture && !driver.status_bar_tap);
  XpAppMappedSnapshot legacy;
  std::memset(&legacy, 0xa5, sizeof(legacy));
  mappedSnapshot(0, 0, 1, &legacy);
  assert(updates == 1 && mappedReads == 1 && touchReads == 0 && keyReads == 4 && releases == 1);
  assert(legacy.home_gesture && legacy.light_panel_gesture && legacy.status_bar_tap && legacy.cpu_hold);
  assert(!legacy.input.pressed && !legacy.input.released && !legacy.input.held && !legacy.input.touch_flags);
  assert(!legacy.power_pressed && !legacy.power_released && !legacy.power_held && !legacy.home_key_tapped);
  // Activity input must see an injection that arrives after the hardware poll.
  mappedInputManager.injected = true;
  XpAppMappedSnapshot home;
  mappedSnapshot(0, 1, 1, &home);
  assert(updates == 1 && mappedReads == 46 && touchReads == 1 && keyReads == 4 && releases == 2);
  assert(home.input.pressed == 0x7fff && home.input.released == 0x7fff && home.input.held == 0x7fff);
  assert(home.input.now_ms == 100 && home.input.held_ms == 800 && home.input.swipe == XP_UI_SWIPE_DOWN);
  assert(home.input.touch_x == 17 && home.input.touch_y == 23 && home.input.touch_flags == 7);
  assert(!home.power_pressed && !home.power_released && !home.power_held && !home.home_key_tapped);
  assert(home.home_gesture && home.light_panel_gesture && home.status_bar_tap && home.cpu_hold);
}
'''
with tempfile.TemporaryDirectory(prefix="app-snapshot-") as temporary:
    directory = Path(temporary)
    cpp = directory / "snapshot.cpp"
    cpp.write_text(harness)
    binary = directory / "snapshot"
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                    "-I", str(ROOT / "rust/crates/xp-ffi/include"), str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
