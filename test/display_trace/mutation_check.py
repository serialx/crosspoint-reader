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
parser.add_argument("--workspace", type=Path, default=HERE.parents[1] / "rust")
parser.add_argument("--sdk", type=Path, default=HERE.parents[1] / "freeink-sdk")
parser.add_argument("--kind", choices=("sleep", "cancellation"), default="sleep")
args = parser.parse_args()
work = args.work_dir.resolve()
workspace = args.workspace.resolve()
copy = work / "rust-input"
shutil.copytree(workspace, copy, dirs_exist_ok=True, copy_function=shutil.copy,
                ignore=shutil.ignore_patterns("target"))
(work / "scripts").mkdir(parents=True, exist_ok=True)
shutil.copy(workspace.parent / "scripts/gen_i18n.py", work / "scripts/gen_i18n.py")
shutil.copytree(workspace.parent / "lib/I18n/translations", work / "lib/I18n/translations", dirs_exist_ok=True)
source = copy / "crates/xp-display/src" / ("uc8253.rs" if args.kind == "sleep" else "papermono.rs")
text = source.read_text()
old, new = (("bus.cmd(0x07);", "bus.cmd(0x06);") if args.kind == "sleep" else
            ("abort != self.work_generation", "{ let _ = abort; false }"))
if text.count(old) != 1:
    sys.exit("mutation anchor changed: " + args.kind)
source.write_text(text.replace(old, new))
work.mkdir(parents=True, exist_ok=True)
with (work / "build.log").open("w") as log:
    subprocess.run(["cmake", "-S", str(HERE), "-B", str(work / "build"), f"-DXP_FFI_HOST_WORKSPACE={copy}",
                    f"-DDISPLAY_TRACE_SDK={args.sdk.resolve()}"],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
    target = "display_trace_c3_single" if args.kind == "sleep" else "display_retained_abi_test"
    subprocess.run(["cmake", "--build", str(work / "build"), "--target", target, "-j", "2"],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
if args.kind == "cancellation":
    result = subprocess.run([str(work / "build/display_retained_abi_test")], capture_output=True, text=True, timeout=10)
    (work / "comparison.log").write_text(result.stdout + result.stderr)
    if result.returncode == 0 or "assertion" not in result.stderr.lower() or "result ==" not in result.stderr:
        sys.exit("cancellation mutation did not fail the overlap result assertion: " + result.stdout + result.stderr)
    print("Detected disabled Paper Mono cancellation in the overlap result assertion")
    sys.exit(0)
result = subprocess.run([sys.executable, str(HERE / "oracle.py"), "verify", "--bin-dir", str(work / "build"),
                         "--case", "c3-uc8253-single"], capture_output=True, text=True)
(work / "comparison.log").write_text(result.stdout + result.stderr)
if result.returncode != 1 or not all(part in result.stderr for part in ("step sleep", "hex=07", "hex=06")):
    sys.exit("mutation did not produce the expected sleep-command mismatch: " + result.stdout + result.stderr)
print("Detected UC8253 CMD_DEEP_SLEEP 07 -> 06 at step sleep")
print(result.stderr.strip())
