#!/usr/bin/env python3
"""Exercise cache and row-allocation failures through the production image facade."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    args = parser.parse_args()
    started = 'DBG IMG Cache stream started: <cache> (4x4, band 4 rows)'
    cases = {
        -1: [started, 'DBG IMG Cache written: <cache> (4x4, 8 bytes)'],
        0: ['ERR IMG Failed to write cache header: <cache>'],
        1: [started, 'ERR IMG Cache write error at row 0'],
        2: [started, 'ERR IMG Cache write error at row 2'],
        3: [started, 'ERR IMG Failed to close cache: <cache>'],
        10: ['ERR IMG Cache band too small (3 < 131070 rows) for 32767x32767'],
        11: ['ERR PNG Failed to allocate PNG line buffers (4 bytes)'],
        12: ['ERR IMG OOM cache band: 5 bytes'],
        13: ['ERR IMG Failed to open cache file for writing: <cache>'],
        14: ['ERR GIF OOM: GIF row buffers (4 bytes each)'],
        15: ['ERR GIF Degenerate output dimensions 0x1 for '],
        16: ['ERR JPG Degenerate output dimensions 0x1 for , skipping render'],
    }
    failures = []
    with tempfile.TemporaryDirectory(prefix='cache-diag-') as tmp:
        path = str(Path(tmp) / 'image.px2')
        for scenario, expected in cases.items():
            cache = path if scenario != 13 else path + '/missing'
            result = subprocess.run(
                [str(args.binary.resolve()), str(scenario), cache], capture_output=True,
                env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'), check=False)
            # JPEG summaries have their own upstream corpus oracle.
            actual = [line for line in result.stderr.decode().replace(cache, '<cache>').splitlines()
                      if not line.startswith('DBG JPG JPEG ')]
            if result.returncode or actual != expected:
                failures.append((scenario, expected, actual, result.returncode))
    for failure in failures:
        print(failure)
    print(f'{len(cases)} scenarios; {len(failures)} failures')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
