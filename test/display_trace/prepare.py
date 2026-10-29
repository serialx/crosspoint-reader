#!/usr/bin/env python3
"""Stage exact SDK sources and add host-only BUSY scope annotations."""
import pathlib
import re
import shutil
import sys

sdk, out, stubs = map(pathlib.Path, sys.argv[1:])
source = sdk / "libs/display/FreeInkDisplay"
shutil.copytree(source / "src", out / "src", dirs_exist_ok=True, copy_function=shutil.copy)
shutil.copytree(source / "include", out / "include", dirs_exist_ok=True, copy_function=shutil.copy)
for header in ("BoardConfig.h", "PanelDrivers.h"):
    source_header = sdk / "libs/hardware/BoardConfig/include" / header
    if source_header.exists():
        shutil.copy(source_header, out / header)
# This header is quoted by BoardConfig; place the host rail hook beside it.
shutil.copy(stubs / "MetalioEInk4Board.h", out / "MetalioEInk4Board.h")
# Track the retained UltraChip planes without interposing host STL allocations.
for name in ("Uc8179", "Uc8279X4", "Uc8279cA4", "RustPanel"):
    driver = out / "src/driver" / f"{name}Driver.cpp"
    text = driver.read_text().replace("std::malloc(", "trace::allocateDefault(")
    text = re.sub(r"(?<![\w:])malloc\(", "trace::allocateDefault(", text)
    driver.write_text('#include "Trace.h"\n' + text)
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
