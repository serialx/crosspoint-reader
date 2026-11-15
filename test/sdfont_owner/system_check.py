"""Frozen native system vs Rust owner: snapshots/order, requests and fault frontiers."""
from pathlib import Path
import argparse
import json
import subprocess
import difflib

ROOT = Path(__file__).resolve().parents[2]
BASE = 'b180cc0605ca2c76ab3fcf58f5d488d4a0741011'


def configure(output):
    output.mkdir(parents=True, exist_ok=True)
    for side in ('parent', 'candidate'):
        folder = output / side
        folder.mkdir(exist_ok=True)
        for name in ('SdCardFontSystem.cpp', 'SdCardFontSystem.h'):
            data = (subprocess.check_output(['git', 'show', f'{BASE}:src/{name}'], cwd=ROOT)
                    if side == 'parent' else (ROOT / 'src' / name).read_bytes())
            dest = folder / name
            if not dest.exists() or dest.read_bytes() != data:
                dest.write_bytes(data)


def run(exe, fault):
    r = subprocess.run([str(exe), *map(str, fault)], text=True, capture_output=True)
    if r.returncode not in (0, 70):
        raise AssertionError(f'{exe} {fault}: exit {r.returncode}\n{r.stdout}\n{r.stderr}')
    return r.returncode, r.stdout


def comparable(text):
    # libc++ has a 24-byte string and destroys vectors backwards; firmware uses
    # GCC14 libstdc++ (24-byte C3 string, forward destruction). Short names here
    # avoid host SSO growth. Compare family capacities, and free multisets within
    # each allocation/I/O/save barrier; real C3 traces still gate exact order.
    lines = text.splitlines()
    width = int(lines[0].split()[1])
    output, frees = [], []
    for line in lines[1:]:
        words = line.split()
        if len(words) > 2 and words[1] == 'FAMILY':
            size = int(words[2])
            assert size % width == 0
            words[2] = str(size // width)
            line = ' '.join(words)
        if words[0] == 'FREE':
            frees.append(line)
        else:
            output.extend(sorted(frees)); frees.clear()
            output.append(line)
    return output + sorted(frees)


def check(parent, candidate):
    baseline = run(parent, (0, 0, 0))
    assert baseline[0] == 0
    total = baseline[1].splitlines()[-1].split()
    assert total[0] == 'TOTAL', baseline
    allocations, operations, saves = map(int, total[1:])
    cases = [(0, 0, 0)] + [(i, 0, 0) for i in range(1, allocations + 1)]
    cases += [(0, i, 0) for i in range(1, operations + 1)]
    cases += [(0, 0, i) for i in range(1, saves + 1)]
    for fault in cases:
        a, b = run(parent, fault), run(candidate, fault)
        if a[0] != b[0] or comparable(a[1]) != comparable(b[1]):
            diff = ''.join(difflib.unified_diff(a[1].splitlines(True), b[1].splitlines(True), fromfile='parent', tofile='candidate'))
            raise AssertionError(f'fault={fault}, exits={a[0]}/{b[0]}\n{diff}')
    print(json.dumps({'base': BASE, 'cases': len(cases), 'allocations': allocations,
                      'file_operations': operations, 'saves': saves, 'equal': True, 'host_layout_classification': 'family capacity; contiguous free multisets', 'c3_heap_order_claim': False}))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--configure', type=Path)
    p.add_argument('--parent', type=Path)
    p.add_argument('--candidate', type=Path)
    args = p.parse_args()
    if args.configure:
        configure(args.configure)
    else:
        check(args.parent, args.candidate)
