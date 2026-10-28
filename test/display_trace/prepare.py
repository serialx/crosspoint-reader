#!/usr/bin/env python3
"""Stage exact SDK sources and add host-only BUSY scope annotations."""
import pathlib
import shutil
import sys

sdk, out, stubs = map(pathlib.Path, sys.argv[1:])
source = sdk / "libs/display/FreeInkDisplay"
shutil.copytree(source / "src", out / "src", dirs_exist_ok=True, copy_function=shutil.copy)
shutil.copytree(source / "include", out / "include", dirs_exist_ok=True, copy_function=shutil.copy)
shutil.copy(sdk / "libs/hardware/BoardConfig/include/BoardConfig.h", out / "BoardConfig.h")
# This header is quoted by BoardConfig; place the host rail hook beside it.
shutil.copy(stubs / "MetalioEInk4Board.h", out / "MetalioEInk4Board.h")
bus = out / "src/bus/EpdBus.cpp"
text = bus.read_text()
if sys.platform == "darwin":
    text = text.replace("__attribute__((weak))", "__attribute__((weak_import))")
for signature, kind, polarity in (
    ("void EpdBus::waitBusy(BusyPolarity p, const char* tag) {", "poll", "p"),
    ("void EpdBus::waitRefreshComplete(const char* tag) {", "refresh", "_busy"),
):
    if text.count(signature) != 1:
        raise SystemExit(f"BUSY annotation signature changed: {signature}")
    text = text.replace(signature, signature + f'\n  trace::Wait traceWait("{kind}", static_cast<int>({polarity}), tag);')
bus.write_text('#include "Trace.h"\n' + text)
