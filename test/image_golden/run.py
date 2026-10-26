#!/usr/bin/env python3
"""Capture the C++ baseline or verify immutable image-pipeline goldens."""
import argparse
import concurrent.futures
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parent

def digest(data):
    return hashlib.sha256(data).hexdigest()


def unpack(data):
    offset = 64  # dimensions + three streaming-probe schedules + decode result
    metadata = list(struct.unpack('<16I', data[:offset]))
    def blob():
        nonlocal offset
        n, = struct.unpack_from('<I', data, offset)
        offset += 4
        result = data[offset:offset+n]
        offset += n
        return result
    frame, calls = blob(), blob()
    exists, = struct.unpack_from('<I', data, offset)
    offset += 4
    cache = blob()
    assert offset == len(data)
    return {'status': metadata, 'frame': digest(frame), 'calls': digest(calls), 'call_bytes': len(calls),
            'cache_exists': exists, 'cache': digest(cache), 'cache_bytes': len(cache)}


def require_case_keys(actual, expected):
    missing = expected.keys() - actual.keys()
    unexpected = actual.keys() - expected.keys()
    if missing or unexpected:
        raise SystemExit(f'Case keys differ: {len(missing)} missing {sorted(missing)[:12]}, '
                         f'{len(unexpected)} unexpected {sorted(unexpected)[:12]}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'goldens.json.gz')
    parser.add_argument('--workers', type=int, default=6)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='image-golden-') as directory:
        directory = Path(directory)
        with tarfile.open(ROOT / 'corpus.tar.gz') as archive:
            archive.extractall(directory, filter='data')
        images = sorted(p for p in directory.iterdir() if p.is_file())
        # Full product for every orientation, plane, polarity and strip target.
        jobs = [(p, variant, target) for p in images for variant in range(11) for target in range(48)]
        for path in images:
            variants = (11, 12, 13) if path.name == 'RGBA-37x29.png' else (12, 13) if path.name in (
                'rgb-801x601-2-0.jpg', 'gif-37x29-0-0.gif') else ()
            jobs.extend((path, variant, target) for variant in variants for target in range(48))
        def run(job):
            path, variant, target = job
            key = f'{path.name}/{variant}/{target}'
            cache = directory / f'{path.name}-{variant}-{target}.pxc'
            result = subprocess.run([str(args.binary.resolve()), str(path), str(cache), str(variant), str(target)], capture_output=True, check=True)
            return key, unpack(result.stdout)
        actual = {}
        if args.capture:
            with tarfile.open(ROOT / 'samples.tar.gz', 'w:gz') as archive:
                for target in (0, 4, 8, 36):
                    path = directory / 'RGBA-37x29.png'
                    data = subprocess.check_output([str(args.binary.resolve()), str(path), str(directory / 'sample.pxc'), '2', str(target)])
                    info = tarfile.TarInfo(f'RGBA-37x29-2-{target}.bin')
                    info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
            for i, (key, result) in enumerate(pool.map(run, jobs)):
                actual[key] = result
                if i % 5000 == 0: print(f'{i}/{len(jobs)}', flush=True)
        document = {'revision': '5d6523086a696abac78a9816f13d0956ab5ff61c',
                    'corpus': {p.name: digest(p.read_bytes()) for p in images}, 'cases': actual}
        if args.capture:
            args.output.write_bytes(gzip.compress(json.dumps(document, sort_keys=True, separators=(',', ':')).encode(), mtime=0))
        else:
            expected = json.loads(gzip.decompress(args.output.read_bytes()))
            require_case_keys(actual, expected['cases'])
            failures = [key for key in actual if actual[key] != expected['cases'].get(key)]
            if document['corpus'] != expected['corpus']: raise SystemExit('Corpus differs')
            if failures:
                for key in failures[:12]:
                    print(key, {k: (expected['cases'][key][k], actual[key][k]) for k in actual[key] if actual[key][k] != expected['cases'][key][k] and k != 'cache'})
                raise SystemExit(f'{len(failures)} / {len(actual)} cases differ')
        print(f'{len(images)} images, {len(actual)} cases passed', flush=True)

if __name__ == '__main__':
    main()
