#!/usr/bin/env python3
"""Replay plugin public interfaces against scripted platform leaves."""
import argparse
import difflib
import gzip
import pathlib
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
p = argparse.ArgumentParser()
p.add_argument('--build', type=pathlib.Path, required=True)
p.add_argument('--source', type=pathlib.Path, default=ROOT)
p.add_argument('--archive', type=pathlib.Path, required=True)
p.add_argument('--json-include', type=pathlib.Path, default=ROOT/'.pio/libdeps/default/ArduinoJson/src')
p.add_argument('--record', action='store_true')
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
cmd = ['c++', '-std=c++20', '-O2', '-I'+str(HERE/'stubs'), '-I'+str(a.source/'src'), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'), '-I'+str(a.json_include), str(HERE/'oracle.cpp')]
cmd += [str(a.source/'src/util'/f'{name}.cpp') for name in ('PluginEvents','PluginHttp','PluginLocations')]
cmd += [str(ROOT/'test/cmake/xp_ffi_host.cpp'), str(a.archive), '-o', str(a.build/'oracle')]
subprocess.run(cmd, check=True)
actual = b''.join(b'CASE '+str(i).encode()+b'\n'+subprocess.check_output([str(a.build/'oracle'),str(i)]) for i in range(34))
(a.build/'actual.txt').write_bytes(actual)
golden = HERE/'golden.txt.gz'
if a.record:
    golden.write_bytes(gzip.compress(actual,mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes())
    if expected != actual:
        print(''.join(difflib.unified_diff(expected.decode().splitlines(True),actual.decode().splitlines(True)))[:16000])
        raise SystemExit('plugin oracle differs')
print('plugin policy: 34 cases passed')
