#!/usr/bin/env python3
"""Verify SSD1677 custom fields and debug output against the pinned C++ traces."""
import argparse
import lzma
from pathlib import Path
import subprocess

from oracle import compare

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--bin-dir', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent / 'ssd-config-goldens'
passed = True
for debug in ('absent', '0', '1'):
    for variant in range(24):
        name = f'{debug}-{variant}'
        actual = subprocess.check_output([str(args.bin_dir.resolve() / f'ssd1677_config_{debug}'), str(variant)])
        passed &= compare(lzma.decompress((root / (name + '.trace.xz')).read_bytes()), actual, name)
print('72 SSD1677 config/debug traces: ' + ('PASS' if passed else 'FAIL'))
raise SystemExit(int(not passed))
