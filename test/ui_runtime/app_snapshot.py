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
harness = r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <string>
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
struct Activity {
  std::string name = "EpubReader";
  unsigned delayCalls = 0;
  virtual ~Activity() = default;
  virtual bool isReaderActivity() const { return false; }
  bool acceptsRemoteInput() const { return true; }
  bool preventAutoSleep() const { return false; }
  bool requiresExclusiveStorageLoop() const { return false; }
  bool skipLoopDelay() { ++delayCalls; return true; }
};
struct ReaderActivity : Activity {
  unsigned overlayCalls = 0;
  bool isReaderActivity() const override { return true; }
  bool inputOverPage() { ++overlayCalls; return true; }
};
''' + body + "\nauto legacyFacts = " + facts + ";\n" + r'''
int main() {
  ReaderActivity reader;
  const auto summary = legacyFacts(&reader, 0, 0);
  assert(summary.reader && summary.remote_input && !summary.skip_delay && !summary.input_over_page);
  assert(reader.delayCalls == 0 && reader.overlayCalls == 0);
  const auto delay = legacyFacts(&reader, 1, 0);
  assert(delay.skip_delay && !delay.input_over_page && reader.delayCalls == 1 && reader.overlayCalls == 0);
  const auto overlay = legacyFacts(&reader, 0, 1);
  assert(!overlay.skip_delay && overlay.input_over_page && reader.delayCalls == 1 && reader.overlayCalls == 1);
  Activity settings;
  settings.name = "Settings";
  const auto native = legacyFacts(&settings, 1, 1);
  assert(native.kind == XP_APP_SETTINGS && !native.reader && native.skip_delay && !native.input_over_page);

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
