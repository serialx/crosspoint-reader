#!/usr/bin/env python3
"""Run every reached short-write point without replacing the historical goldens."""
import argparse
import json
import re
import subprocess
from pathlib import Path


def observations(binary):
    def run(index):
        return subprocess.run([str(binary), 'short', str(index)], capture_output=True,
                              text=True, check=True, timeout=20).stdout
    normal = run(0)
    count = int(re.search(r'^writes (\d+)$', normal, re.M)[1])
    assert count > 10
    assert 'create 1\n' in normal
    return {str(index): normal if index == 0 else run(index) for index in range(count + 1)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--compare', type=Path)
    args = parser.parse_args()
    actual = observations(args.binary.resolve())
    if args.capture:
        with args.capture.open('x') as file:
            json.dump(actual, file, indent=2)
            file.write('\n')
    if args.compare:
        expected = json.loads(args.compare.read_text())
        assert actual.keys() == expected.keys(), 'short-write inventory changed'
        for key in expected:
            assert actual[key] == expected[key], f'short-write {key} changed'
    print(f'PASS {len(actual) - 1} short-write points')
