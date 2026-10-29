"""Frozen activity methods with deterministic service leaves and an ordered transcript."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
import unittest
HERE = Path(__file__).resolve().parent


def record(mutate=False):
    original = (HERE / 'original.inc').read_text()
    if mutate:
        original = original.replace('delay(1500);', 'delay(1501);')
    labels = sorted(set(re.findall(r'tr\((STR_\w+)\)', original)))
    defines = '\n'.join(f'#define {s} "{s}"' for s in labels) + '\n#define tr(x) x\n'
    source = (HERE / 'effects.cpp').read_text().replace('// LABELS', defines)
    source = source.replace('// ORIGINAL', original).replace('// CASES', (HERE / 'effects_cases.inc').read_text())
    # Fixed build marker makes the diagnostic independent of host wall time.
    source = source.replace('__DATE__', '"Oct 29 2026"').replace('__TIME__', '"00:00:00"')
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / 'effects.cpp'
        exe = Path(directory) / 'effects'
        cpp.write_text(source)
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Wno-unused-parameter', str(cpp), '-o', str(exe)], check=True)
        return subprocess.check_output([str(exe)])


class Effects(unittest.TestCase):
    def test_frozen_transcript(self):
        self.assertEqual(record(), (HERE / 'effects.golden').read_bytes())

    def test_mutation_detected(self):
        self.assertNotEqual(record(True), (HERE / 'effects.golden').read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--record', action='store_true')
    args = parser.parse_args()
    if args.record:
        (HERE / 'effects.golden').write_bytes(record())
    else:
        unittest.main(argv=['effects.py'])
