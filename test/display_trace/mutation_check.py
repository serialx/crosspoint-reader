#!/usr/bin/env python3
"""Prove the oracle detects a one-byte controller change without editing the SDK."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(__doc__)
parser.add_argument("--work-dir", type=Path, required=True)
args = parser.parse_args()
work = args.work_dir.resolve()
sdk = HERE.parents[1] / "freeink-sdk"
copy = work / "sdk-input"
for folder in ("libs/display/FreeInkDisplay", "libs/hardware/BoardConfig/include"):
    shutil.copytree(sdk / folder, copy / folder, dirs_exist_ok=True, copy_function=shutil.copy)
source = copy / "libs/display/FreeInkDisplay/src/driver/Uc8253X3Driver.cpp"
text = source.read_text()
old = "constexpr uint8_t CMD_DEEP_SLEEP = 0x07;"
if text.count(old) != 1:
    sys.exit("sleep-command mutation anchor changed")
source.write_text(text.replace(old, "constexpr uint8_t CMD_DEEP_SLEEP = 0x06;"))
work.mkdir(parents=True, exist_ok=True)
with (work / "build.log").open("w") as log:
    subprocess.run(["cmake", "-S", str(HERE), "-B", str(work / "build"), f"-DDISPLAY_TRACE_SDK={copy}"],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run(["cmake", "--build", str(work / "build"), "--target", "display_trace_c3_single", "-j", "2"],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
result = subprocess.run([sys.executable, str(HERE / "oracle.py"), "verify", "--bin-dir", str(work / "build"),
                         "--case", "c3-uc8253-single"], capture_output=True, text=True)
(work / "comparison.log").write_text(result.stdout + result.stderr)
if result.returncode != 1 or not all(part in result.stderr for part in ("step sleep", "hex=07", "hex=06")):
    sys.exit("mutation did not produce the expected sleep-command mismatch: " + result.stdout + result.stderr)
print("Detected UC8253 CMD_DEEP_SLEEP 07 -> 06 at step sleep")
print(result.stderr.strip())
