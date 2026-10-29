#!/usr/bin/env python3
"""Freeze/replay actual OTA C++ facades with deterministic platform leaves."""
import argparse
import difflib
import gzip
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--source', type=Path, default=ROOT)
p.add_argument('--archive', type=Path, required=True)
p.add_argument('--record', action='store_true')
p.add_argument('--usb', action='store_true')
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
sources = [HERE/'oracle.cpp', ROOT/'test/cmake/xp_ffi_host.cpp', ROOT/'lib/JsonParser/ReleaseJsonParser.cpp', ROOT/'freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp']
for name in ('OtaUpdater', 'OtaBootSwitch', 'FirmwareBoardTag'):
    dest = a.build/(name+'.cpp')
    shutil.copyfile(a.source/'src/network'/(name+'.cpp'), dest)
    sources.append(dest)
cmd = ['c++', '-std=c++20', '-O2', '-DCROSSPOINT_VERSION=oracle::currentVersion.c_str()']
for path in (HERE/'stubs', a.source/'src/network', a.source/'rust/crates/xp-ffi/include', ROOT/'lib/JsonParser', ROOT/'freeink-sdk/libs/network/JsonSax/include', ROOT/'rust/crates/xp-ffi/include'):
    cmd.append('-I'+str(path))
if a.usb:
    sources = [HERE/'usb.cpp', a.source/'src/platform/UsbSerialJtagHandoff.cpp', ROOT/'test/cmake/xp_ffi_host.cpp']
    cmd += ['-I'+str(a.source/'src/platform'), '-DARDUINO_ARCH_ESP32=1', '-DCONFIG_IDF_TARGET_ESP32S3=1', '-DFREEINK_CAP_USB_MSC=1']
exe = a.build/'oracle' 
subprocess.run(cmd+[str(s) for s in sources]+[str(a.archive), '-o', str(exe)], check=True)
result = subprocess.check_output([str(exe)])
for mode, count in (() if a.usb else (("install-power", 20), ("boot-power", 8))):
    for cut in range(count):
        result += f"CASE {mode}-{cut}\n".encode() + subprocess.check_output([str(exe),mode,str(cut)])
(a.build/'actual.txt').write_bytes(result)
golden = HERE/('usb-golden.txt.gz' if a.usb else 'golden.txt.gz')
if a.record:
    golden.write_bytes(gzip.compress(result, mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes())
    if expected != result:
        print(''.join(difflib.unified_diff(expected.decode().splitlines(True), result.decode().splitlines(True)))[:16000])
        raise SystemExit('OTA oracle differs')
print('OTA oracle:', result.count(b'CASE '), 'cases passed')
