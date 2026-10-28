#!/usr/bin/env python3
"""Distinguish GIF gap processing from final cache-write failures."""
import argparse
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('fixtures', type=Path)
    parser.add_argument('--frame-oracle', type=Path)
    parser.add_argument('--baseline-frame', type=Path)
    parser.add_argument('--exceptions', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('gif', args.fixtures)
    gif = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gif)
    cases = {
        5: 'ERR GIF GIF post-process failed: fixture.gif',
        17: 'ERR GIF Failed to finalize pixel cache: fixture.px2',
    }
    failures = []
    records = {}
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
        if args.exceptions:
            exceptions = json.loads(args.exceptions.read_text())['cases']
            fixtures = gif.fixtures()
            for name, expected in exceptions.items():
                data = fixtures[name]
                assert hashlib.sha256(data).hexdigest() == expected['sha256'], name
                path = Path(tmp) / (name + '.gif')
                cache = str(path) + '.px2'
                path.write_bytes(data)
                outputs = []
                for binary in [args.frame_oracle] + ([args.baseline_frame] if args.baseline_frame else []):
                    result = subprocess.run(
                        [str(binary.resolve()), str(path), cache, '6', '0'], capture_output=True,
                        env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'), check=True)
                    outputs.append(result.stdout)
                    # The oracle records decode status, two blobs, cache presence, and cache bytes.
                    assert struct.unpack_from('<I', result.stdout, 60)[0] == 0, name
                    offset = 64
                    for _ in range(2):
                        offset += 4 + struct.unpack_from('<I', result.stdout, offset)[0]
                    assert struct.unpack_from('<I', result.stdout, offset)[0] == 0, name
                    if binary == args.frame_oracle:
                        logs = result.stderr.decode().replace(cache, '<cache>').replace(str(path), '<input>').splitlines()
                        assert logs == expected['logs'], (name, logs)
                        assert logs[-2].startswith('DBG IMG Cache stream started:')
                        assert logs[-1].startswith('ERR GIF AnimatedGIF open failed:')
                if args.baseline_frame:
                    assert outputs[0] == outputs[1], name
                records[name] = dict(decode=False, cache_present=False, output_sha256=hashlib.sha256(outputs[0]).hexdigest(),
                                     baseline_bytes_equal=bool(args.baseline_frame))
    if args.report:
        args.report.write_text(json.dumps(records, indent=2))
    print(failures if failures else 'GIF completion failures, inherited log order, and cache cleanup passed')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
