#!/usr/bin/env python3
"""Compile the actual native restart-phase lambda against observed RTC/reset state."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / "src/main.cpp").read_text()
start = text.index("[](uint8_t phase, uint8_t lightOn) -> uint8_t {")
body = text[start : text.index("      .sleep_enter =", start)].rstrip()
assert body.endswith(",")
source = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include "xp_app.h"
static bool deepSleepInProgress;
static uint32_t silentRebootTarget, silentRebootPayload, silentRebootMagic;
static constexpr uint32_t SILENT_REBOOT_TARGET_READER=2, SILENT_REBOOT_LIGHT_ON=1, SILENT_REBOOT_MAGIC=0x1234;
static std::vector<int> calls;
#define LOG_DBG(...) calls.push_back(1)
struct { void restart() { calls.push_back(2); } } ESP;
static auto phase = BODY;
int main() {
  deepSleepInProgress=true;
  assert(phase(XP_APP_BLE_RESTART_ALLOWED,0)==0);
  assert(calls.empty() && !silentRebootMagic);
  deepSleepInProgress=false;
  assert(phase(XP_APP_BLE_RESTART_ALLOWED,0)==1);
  assert(calls.empty() && !silentRebootMagic);
  for (uint8_t light : {0,1}) {
    phase(XP_APP_BLE_RESTART_ARM, light);
    assert(silentRebootTarget==SILENT_REBOOT_TARGET_READER);
    assert(silentRebootPayload==(light ? SILENT_REBOOT_LIGHT_ON : 0));
    assert(silentRebootMagic==SILENT_REBOOT_MAGIC);
    assert(calls.back()==1);
    phase(XP_APP_BLE_RESTART_EXECUTE,0);
    assert(calls.back()==2);
  }
  assert((calls==std::vector<int>{1,2,1,2}));
}
'''.replace("BODY", body[:-1])
with tempfile.TemporaryDirectory(prefix="runtime-ble-") as temp:
    folder = Path(temp)
    (folder / "check.cpp").write_text(source)
    subprocess.run(
        ["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
         "-I", str(ROOT / "rust/crates/xp-ffi/include"), str(folder / "check.cpp"),
         "-o", str(folder / "check")], check=True,
    )
    subprocess.run([str(folder / "check")], check=True)
print("PASS native restart phases: sleep guard, live-light RTC payload, reset")
