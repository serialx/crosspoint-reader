#!/usr/bin/env python3
"""Replay upstream image milestones across the public corpus and render settings."""
import argparse
import concurrent.futures
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

PREFIXES = (
    'DBG PNG PNG ', 'INF JPG Progressive JPEG detected', 'DBG JPG JPEG ',
    'ERR JPG Degenerate output dimensions', 'ERR GIF Degenerate output dimensions',
    'ERR PNG Failed to allocate PNG line buffers', 'ERR GIF OOM: GIF row buffers',
    'ERR GIF GIF post-process failed:', 'ERR GIF Failed to finalize pixel cache:',
    'ERR IMG Cache band too small', 'ERR IMG OOM cache band:',
    'ERR IMG Failed to open cache file for writing:', 'ERR IMG Failed to write cache header:',
    'DBG IMG Cache stream started:', 'ERR IMG Cache write error at row',
    'ERR IMG Failed to close cache:', 'DBG IMG Cache written:',
)


def selected(lines):
    return [line for line in lines
            if line.startswith(PREFIXES) and 'decoding complete' not in line]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--corpus', type=Path, required=True)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--goldens', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    frozen = json.loads(gzip.decompress(args.goldens.read_bytes())) if args.goldens else None
    with tempfile.TemporaryDirectory(prefix='pipeline-diag-') as tmp:
        root = Path(tmp)
        jobs, inputs = [], {}
        with tarfile.open(args.corpus) as archive:
            for member in archive.getmembers():
                if not member.isfile():
                    continue
                data = archive.extractfile(member).read()
                path = root / Path(member.name).name
                path.write_bytes(data)
                inputs[member.name] = hashlib.sha256(data).hexdigest()
                for variant in range(14):
                    jobs.append((member.name, variant, path))

        def run(job):
            name, variant, path = job
            cache = root / f'{path.name}-{variant}.px2'
            result = subprocess.run(
                [str(args.binary.resolve()), str(path), str(cache), str(variant), '0'],
                env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'), capture_output=True,
                timeout=10, check=False)
            logs = selected(result.stderr.decode().splitlines())
            logs = [line.replace(str(cache), '<cache>').replace(str(path), '<input>')
                    for line in logs]
            return f'{name}/{variant}', dict(logs=logs, exit=result.returncode)

        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            results = dict(pool.map(run, jobs))
    document = dict(revision='116195b4', inputs=inputs, cases=results)
    if args.capture:
        args.capture.write_bytes(gzip.compress(json.dumps(document, sort_keys=True).encode(), mtime=0))
    failures = {}
    if frozen:
        assert inputs == frozen['inputs'] and results.keys() == frozen['cases'].keys()
        failures = {
            key: dict(reference=frozen['cases'][key], candidate=value)
            for key, value in results.items()
            if sorted(value['logs']) != sorted(frozen['cases'][key]['logs'])
            or value['exit'] != frozen['cases'][key]['exit']
        }
    args.report.write_text(json.dumps(dict(cases=len(results), differences=failures), indent=2))
    print(f'{len(results)} cases; {len(failures)} differences')
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
