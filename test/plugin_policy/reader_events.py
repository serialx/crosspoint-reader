#!/usr/bin/env python3
"""Compare whole outboxes and file traces from native and mixed Rust/native events."""
import argparse
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--archive', type=Path, required=True)
p.add_argument('--json-include', type=Path, required=True)
p.add_argument('--baseline-source', type=Path, default=ROOT)
a = p.parse_args()
a.build.mkdir(parents=True, exist_ok=True)
for mode, source in [('native', a.baseline_source), ('mixed', ROOT)]:
    cmd = ['c++', '-std=c++20', '-DXP_SD_FONT_CATALOG', '-O2', '-fsanitize=address',
           '-fno-omit-frame-pointer', '-I'+str(HERE/'stubs'), '-I'+str(ROOT/'src'),
           '-I'+str(ROOT/'rust/crates/xp-ffi/include'), '-I'+str(a.json_include),
           '-I'+str(ROOT/'lib/HttpHeader'), str(HERE/'reader_events.cpp'),
           str(HERE/'HttpGuardLeaves.cpp'), str(ROOT/'test/cmake/xp_ffi_host.cpp'),
           *[str(source/'src/util'/f'{name}.cpp') for name in ('PluginEvents', 'PluginHttp', 'PluginLocations')],
           str(a.archive), '-o', str(a.build/mode)]
    if mode == 'native':
        cmd.append('-DPLUGIN_READER_NATIVE_ONLY')
    subprocess.run(cmd, check=True)
for scenario in range(8):
    outputs = []
    for mode in ['native', 'mixed']:
        output = subprocess.check_output([str(a.build/mode), str(scenario)])
        (a.build/f'{mode}-{scenario}.txt').write_bytes(output)
        outputs.append(output)
    assert outputs[0] == outputs[1], f'outbox/file trace changed in scenario {scenario}'
print('reader events: 8 native/mixed pairs, exact outbox bytes and file traces passed')
