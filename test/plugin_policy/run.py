#!/usr/bin/env python3
"""Replay plugin public interfaces against scripted platform leaves."""
import argparse
import difflib
import gzip
import pathlib
import re
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
p = argparse.ArgumentParser()
p.add_argument('--build', type=pathlib.Path, required=True)
p.add_argument('--source', type=pathlib.Path, default=ROOT)
p.add_argument('--archive', type=pathlib.Path, required=True)
p.add_argument('--json-include', type=pathlib.Path, default=ROOT/'.pio/libdeps/default/ArduinoJson/src')
p.add_argument('--record', action='store_true')
p.add_argument('--json-faults', action='store_true')
p.add_argument('--asan', action='store_true')
p.add_argument('--token-lifetime', action='store_true')
p.add_argument('--memory-baseline', type=pathlib.Path)
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
cmd = ['c++', '-std=c++20', '-O2', '-I'+str(HERE/'stubs'), '-I'+str(a.source/'src'), '-I'+str(ROOT/'src'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'), '-I'+str(a.json_include), str(HERE/'oracle.cpp')]
if a.token_lifetime:
    cmd += ['-DPLUGIN_TOKEN_LIFETIME', str(HERE/'allocation_probe.cpp')]
if a.asan:
    cmd += ['-fsanitize=address', '-fno-omit-frame-pointer', '-g']
if a.json_faults:
    cmd.append('-DPLUGIN_JSON_FAULTS')
cmd += [str(a.source/'src/util'/f'{name}.cpp') for name in ('PluginEvents','PluginHttp','PluginLocations')]
cmd += [str(ROOT/'test/cmake/xp_ffi_host.cpp'), str(a.archive), '-o', str(a.build/'oracle')]
subprocess.run(cmd, check=True)
cases = [(i, 0) for i in range(48)]
if a.json_faults:
    cases += [(i, fault) for i in (0,7,16,22,23,29,40,43) for fault in range(1,113)]
if a.token_lifetime:
    cases = [(48, 0)]
traces = []
metrics = []
for i, fault in cases:
    result = subprocess.check_output([str(a.build/'oracle'), str(i), str(fault)])
    lines = result.splitlines(keepends=True)
    metrics += [f"CASE {i} FAULT {fault} ".encode() + line for line in lines if line.startswith(b'MEM ')]
    traces.append(f"CASE {i} FAULT {fault}\n".encode() + b''.join(line for line in lines if not line.startswith(b'MEM ')))
actual = b''.join(traces)
(a.build/'actual.txt').write_bytes(actual)
(a.build/'memory.txt').write_bytes(b''.join(metrics))
golden = HERE/('json-fault-golden.txt.gz' if a.json_faults else 'golden.txt.gz')
if a.token_lifetime:
    memory = b''.join(metrics).decode()
    print(memory, end='')
    if not a.record:
        assert int(re.search(r'toast_large=(\d+)', memory)[1]) == 4, 'duplicate token retained through toast'
    if a.memory_baseline:
        assert actual == (a.memory_baseline/'actual.txt').read_bytes(), 'token/retry/toast bytes changed'
        before = (a.memory_baseline/'memory.txt').read_text()
        for field in ('toast_peak', 'toast_live'):
            delta = int(re.search(field+r'=(\d+)', before)[1]) - int(re.search(field+r'=(\d+)', memory)[1])
            assert delta >= 8174, f'{field}: duplicate token was not released'
            print(f'{field} reduction: {delta} bytes')
elif a.record:
    golden.write_bytes(gzip.compress(actual,mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes())
    if expected != actual:
        print(''.join(difflib.unified_diff(expected.decode().splitlines(True),actual.decode().splitlines(True)))[:16000])
        raise SystemExit('plugin oracle differs')
print(f'plugin policy: {len(cases)} cases passed')
