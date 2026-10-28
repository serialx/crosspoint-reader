#!/usr/bin/env python3
"""Distinguish GIF gap processing from final cache-write failures."""
import argparse
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('fixtures', type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('gif', args.fixtures)
    gif = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gif)
    cases = {
        5: 'ERR GIF GIF post-process failed: fixture.gif',
        17: 'ERR GIF Failed to finalize pixel cache: fixture.px2',
    }
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'gap.gif'
        data = bytearray(gif.SMALL)
        struct.pack_into('<HH', data, 6, 32, 16)
        path.write_bytes(data)
        for failed_write, expected in cases.items():
            result = subprocess.run(
                [str(args.binary.resolve()), str(path), str(failed_write)], capture_output=True,
                env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'), check=False)
            logs = [line for line in result.stderr.decode().splitlines() if line.startswith('ERR GIF')]
            if result.returncode or result.stdout != b'0\n' or logs != [expected]:
                failures.append((failed_write, expected, logs))
    print(failures if failures else 'Both GIF completion failures report their original messages')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
