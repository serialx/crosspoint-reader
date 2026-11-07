#!/usr/bin/env python3
"""Paired scripted C3 limit dispatch, values and physical/log order; no host allocator parity claim."""
import argparse
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
root = Path(__file__).resolve().parent
subprocess.run(['cargo', 'build', '--manifest-path', str(root/'rust/Cargo.toml'), '--bin', 'xtc-boundary',
                '--offline', '--locked', '--target-dir', str(a.out/'target')], check=True)
exe = a.out/'target/debug/xtc-boundary'
count = 0
for kind, values in [('chapter', [76695843, 76695844, 76695845, 153391690, 4294967295]),
                     ('stream', [2147483646, 2147483647, 2147483648, 4294967295])]:
    for n in values:
        outputs = []
        for side in ['parent', 'candidate']:
            run = subprocess.run([str(exe), side, kind, str(n)], capture_output=True)
            (a.out/f'{kind}-{n}-{side}.txt').write_bytes(run.stdout+run.stderr)
            assert run.returncode == 0, (side, kind, n, run.returncode, run.stderr)
            outputs.append(run.stdout)
        assert outputs[0] == outputs[1], (kind, n, outputs)
        count += 1
print(f'{count} paired scripted C3 size boundaries preserve helper, reads and diagnostic order; actual C3 proof required')
