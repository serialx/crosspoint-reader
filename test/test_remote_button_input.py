"""Run with python3 test/test_remote_button_input.py (requires a host C++ compiler)."""

from pathlib import Path
import subprocess
import tempfile
from rust_host import rust_link_flags

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/MappedInputManager.cpp").read_text()
# Exercise production frame/dispatch methods with physical input under test control.
methods = []
for signature, following in (
    ("void MappedInputManager::update(", "bool MappedInputManager::isNavDirectionSwapped("),
    ("bool MappedInputManager::injectRemoteClick(", "bool MappedInputManager::wasLongPressed("),
    ("bool MappedInputManager::isPressed(", "MappedInputManager::Labels MappedInputManager::mapLabels("),
):
    methods.append(signature + source.split(signature, 1)[1].split(following, 1)[0])

stub = r"""
#pragma once
#include <cstdint>
struct HalGPIO {
  bool pressed = false, released = false, held = false;
  uint8_t button = 0;
  void update() {}
  bool wasAnyPressed() const { return pressed; }
  bool wasAnyReleased() const { return released; }
  bool wasPressed(uint8_t b) const { return b == button && pressed; }
  bool wasReleased(uint8_t b) const { return b == button && released; }
  bool isPressed(uint8_t b) const { return b == button && held; }
  unsigned long getHeldTime() const { return 900; }
  bool wasCapacitivePagePressed() const { return false; }
  bool hasHomeKey() const { return false; }
  bool wasHomeKeyTapped() const { return false; }
  bool wasHomeKeyLongPressed() const { return false; }
  bool wasHomeKeyPressed() const { return false; }
};
"""
harness = r"""
#include <cassert>
#include "MappedInputManager.h"
#include "app_leaves.h"
class GfxRenderer {};
MappedInputManager* testInput = nullptr;
struct Compatibility {
  XpAppCompatibility table{};
  struct InputProxy { bool hasRemoteClick() const { return testInput->hasRemoteClick(); } } mappedInput;
  HomeButtonAction homeButtonAction() const {
    return table.home_action ? static_cast<HomeButtonAction>(table.home_action()) : HomeButtonAction::Ignore;
  }
} activityManager;
bool send(XpAppEvent event) { return xp_app_event(&event) == XP_APP_ACCEPTED; }

unsigned long millis() { return 1000; }
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
struct CrossPointSettings {
  enum { VIBRATION_OFF, VIBRATION_TOUCH_PAGE };
  enum { IGNORE = 0, PWR_BOOKMARK = 7, PWR_SYNC = 8, PWR_READER_MENU = 9 };
  int shortPwrBtn = IGNORE;
  int vibration = VIBRATION_OFF, hapticIntensity = 0;
  int homeButtonTapAction = 0, homeButtonDoubleTapAction = 0, homeButtonLongPressAction = 0;
} SETTINGS;
struct HalHaptics {
  static void feedback(bool, bool, int) {}
  static void longPress(bool, int) {}
};
bool MappedInputManager::mapButton(Button b, bool (HalGPIO::*fn)(uint8_t) const) const {
  return (gpio.*fn)(static_cast<uint8_t>(b));
}
bool MappedInputManager::wasBackGesture() const { return false; }
MappedInputManager::SwipeDir MappedInputManager::wasSwipe(int*, int*) const { return SwipeDir::None; }
""" + "\n".join(methods) + r"""
int main() {
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input(gpio, renderer);
  using Button = MappedInputManager::Button;
  struct Mapping { RemoteButton remote; Button button; Button other; };
  for (const auto mapping : {
      Mapping{RemoteButton::Confirm, Button::Confirm, Button::Back},
      Mapping{RemoteButton::Back, Button::Back, Button::Confirm},
      Mapping{RemoteButton::Next, Button::NavNext, Button::NavPrevious},
      Mapping{RemoteButton::Next, Button::Right, Button::Left},
      Mapping{RemoteButton::Previous, Button::NavPrevious, Button::NavNext},
      Mapping{RemoteButton::Previous, Button::Left, Button::Right}}) {
    const auto remote = mapping.remote;
    const auto button = mapping.button;
    const auto other = mapping.other;
    input.update();
    assert(input.injectRemoteClick(remote));
    assert(input.hasRemoteClick());
    assert(input.wasPressed(button) && input.wasReleased(button));
    assert(!input.wasPressed(other) && !input.wasReleased(other));
    assert(!input.isPressed(button) && input.getHeldTime() == 0);
    assert(!input.injectRemoteClick(remote));
    input.update();
    assert(!input.hasRemoteClick());
    assert(!input.wasPressed(button) && !input.wasReleased(button));
    assert(input.getHeldTime() == 900);
  }
  // Physical edges and held Confirm/Back must win over a simultaneous remote click.
  gpio.pressed = true;
  assert(!input.injectRemoteClick(RemoteButton::Confirm));
  gpio.pressed = false;
  gpio.released = true;
  assert(!input.injectRemoteClick(RemoteButton::Back));
  gpio.released = false;
  gpio.held = true;
  for (Button button : {Button::Confirm, Button::Back}) {
    gpio.button = static_cast<uint8_t>(button);
    assert(!input.injectRemoteClick(RemoteButton::Confirm));
    assert(!input.injectRemoteClick(RemoteButton::Back));
  }
  gpio.held = false;
  assert(input.injectRemoteClick(RemoteButton::Back));
}
"""
manager_source = (ROOT / "src/activities/ActivityManager.cpp").read_text()
def method(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    level = 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
remote_turn = method(manager_source, "bool ActivityManager::remoteTurn(")
reader_turn = method(manager_source, "[](void* activity, uint8_t forward, uint8_t chapter)")
owner_helpers = 'struct OwnedLegacyActivity' + manager_source.split('struct OwnedLegacyActivity', 1)[1].split('std::unique_ptr<OwnedLegacyActivity> ownLegacy', 1)[0]
owner_helpers += method(manager_source, 'Activity* nativeActivity(')
harness = harness.replace("int main() {", r"""
#include <atomic>
#include <memory>
#include "activities/reader/RemoteTurnGate.h"
struct RenderLock { static bool peek() { return false; } };
struct ReaderActivity {
  std::atomic<bool> pageRendered{true};
  bool acceptsRemoteInput() const { return pageRendered.load(std::memory_order_acquire); }
  bool menu = false, context = true, foregroundReader = true;
  int pages = 0, chapters = 0;
  bool inputOverPage() const { return menu; }
  bool pageTurn(bool forward) { pages += forward ? 1 : -1; return true; }
  bool skipPages(int step) { chapters += step; return true; }
};
using Activity = ReaderActivity;
LEGACY_OWNER_HELPERS
struct ActivityManager {
  MappedInputManager& mappedInput;
  ReaderActivity* currentActivity = new ReaderActivity();
  bool& context = currentActivity->context;
  bool& foregroundReader = currentActivity->foregroundReader;
  bool isRemoteReaderContext() const { return context; }
  bool isForegroundReader() const { return foregroundReader; }
  bool remoteClick(RemoteButton button) { return mappedInput.injectRemoteClick(button); }
  void requestUpdate() {}
  bool remoteTurn(bool forward, bool chapter);
};
""" + remote_turn + r"""
void checkRouting(MappedInputManager& input) {
  ActivityManager manager{input};
  testInput = &input;
  static auto legacy = appTestLegacy();
  static auto resources = appTestResources();
  static auto runtime = appTestRuntime();
  legacy.destroy = [](void* object) { delete static_cast<OwnedLegacyActivity*>(object); };
  legacy.facts = [](const void* object, uint8_t delay, uint8_t input) -> XpAppLegacyFacts {
    const auto* reader = static_cast<const ReaderActivity*>(nativeActivity(object));
    XpAppLegacyFacts facts{};
    facts.reader = reader->foregroundReader;
    facts.remote_input = reader->context && reader->acceptsRemoteInput();
    facts.input_over_page = input && reader->inputOverPage();
    return facts;
  };
  legacy.reader_turn = READER_TURN_LEAF;
  runtime.input_remote_click = [](uint8_t button) -> uint8_t { return testInput->injectRemoteClick(static_cast<RemoteButton>(button)); };
  runtime.render_lock = []() -> void* { return testInput; };
  runtime.render_try_lock = runtime.render_lock;
  runtime.ble_before_screen_change = [](uint8_t) -> uint8_t { return 1; };
  const XpAppPlatform platform{&legacy, &resources, &runtime};
  assert(xp_app_init(&platform, &activityManager.table) == XP_APP_OK);
  XpAppEvent adopt{.kind = XP_APP_ADOPT_REPLACE};
  adopt.value.adopt_replace = new OwnedLegacyActivity(std::unique_ptr<Activity>(manager.currentActivity));
  assert(send(adopt));
  input.update();
  assert(manager.remoteTurn(true, false));
  assert(manager.currentActivity->pages == 1 && !input.hasRemoteClick());
  xp_app_loop();  // Complete the requested frame before overlay remote-click routing.
  for (bool reader : {true, false}) {
    manager.foregroundReader = reader;
    manager.currentActivity->menu = true;
    for (bool forward : {true, false}) {
      input.update();
      assert(manager.remoteTurn(forward, false));
      assert(input.wasPressed(forward ? MappedInputManager::Button::NavNext
                                     : MappedInputManager::Button::NavPrevious));
      assert(manager.currentActivity->pages == 1);
      assert(!manager.remoteTurn(forward, false));
    }
    input.update();
    assert(!manager.remoteTurn(true, true));
    assert(manager.currentActivity->chapters == 0 && !input.hasRemoteClick());
  }
  manager.context = false;
  assert(!manager.remoteTurn(true, false));
}
int main() {
""")
harness = harness.replace("READER_TURN_LEAF", reader_turn)
harness = harness.replace("  using Button =", "  checkRouting(input);\n  using Button =")
harness = harness.replace("LEGACY_OWNER_HELPERS", owner_helpers)
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "HalGPIO.h").write_text(stub)
    cpp = directory / "remote_input.cpp"
    executable = directory / "remote_input"
    cpp.write_text("#include <initializer_list>\n" + harness)
    subprocess.run([
        "c++", "-std=c++20", "-DFREEINK_CAP_TOUCH=0", "-I", str(directory),
        "-I", str(ROOT / "src"), "-I", str(ROOT / "test/ui_runtime"), str(cpp), *rust_link_flags(), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Remote button input regression checks passed")
