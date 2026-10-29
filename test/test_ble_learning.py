"""Compile the production BLE facade learning loop against the Rust host archive."""
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/settings/BleSettingsActivity.cpp").read_text()
method = "void BleSettingsActivity::stepLearning()" + source.split(
    "void BleSettingsActivity::stepLearning()", 1
)[1].split("void BleSettingsActivity::keepLearned", 1)[0]
harness = r"""
#include <cassert>
#include <cstring>
#include <deque>
#include "BlePageTurner.h"
#include "xp_ui_settings.h"
uint32_t clockMs = 100;
uint32_t millis() { return clockMs; }
#define tr(id) #id
namespace bleturner {
std::deque<Event> events;
Peer linked() { return {"01:02:03:04:05:06", "Remote"}; }
Status status() { Status result{}; result.connected = true; return result; }
bool pollEvent(Event& ev) {
  if (events.empty()) return false;
  ev = events.front(); events.pop_front(); return true;
}
}
struct BleSettingsActivity {
  XpUiSettingsBle state{};
  BleSettingsActivity() { state.action = 1; state.since_ms = clockMs; }
  char learnAddr[18] = "01:02:03:04:05:06";
  int saved = -1;
  void refresh() {}
  void requestUpdate() {}
  void keepLearned(bool hold) { saved = hold; state.action = 0; }
  void stepLearning();
};
""" + method + r"""
void press(BleSettingsActivity& screen) {
  bleturner::Event event{};
  event.code = 0x030002; event.pressed = true; event.atMs = clockMs;
  bleturner::events.push_back(event);
  screen.stepLearning();
}
void release(BleSettingsActivity& screen) {
  bleturner::Event event{};
  event.code = 0x030002; event.atMs = clockMs;
  bleturner::events.push_back(event);
  screen.stepLearning();
}
int main() {
  BleSettingsActivity hold;
  press(hold);
  clockMs += 2100; hold.stepLearning();
  assert(hold.saved == -1);
  clockMs += 900; release(hold);
  assert(hold.saved == 1);

  BleSettingsActivity tap;
  press(tap);
  clockMs += 100; release(tap);
  assert(tap.saved == 0);

  BleSettingsActivity noRelease;
  press(noRelease);
  clockMs += bleturner::kWaitMs - 1; noRelease.stepLearning();
  assert(noRelease.saved == -1);
  ++clockMs; noRelease.stepLearning();
  assert(noRelease.saved == 0);

  BleSettingsActivity noPress;
  clockMs += bleturner::kWaitMs; noPress.stepLearning();
  assert(noPress.saved == -1 && noPress.state.action == 0);
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "learning.cpp"
    executable = Path(directory) / "learning"
    cpp.write_text(harness)
    subprocess.run(["c++", "-std=c++20", "-I", str(ROOT / "lib/BlePageTurner/include"),
                    "-I", str(ROOT / "freeink-sdk/libs/network/BleKeyboardHost/include"),
                    "-I", str(ROOT / "rust/crates/xp-ffi/include"),
                    str(cpp), str(ROOT / "test/cmake/xp_ffi_host.cpp"), sys.argv[1],
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("Bluetooth learning regression checks passed")
