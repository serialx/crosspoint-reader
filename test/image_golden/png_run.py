#!/usr/bin/env python3
"""Freeze or replay PNG consumer and mutation outcomes, including old hangs."""
import argparse
import concurrent.futures
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
from png_cases import cases

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--output', type=Path, default=ROOT / 'png-goldens.json.gz')
    parser.add_argument('--actual', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        with tarfile.open(ROOT / 'corpus.tar.gz') as archive:
            archive.extractall(tmp, filter='data')
        inputs = {p.stem: p.read_bytes() for p in tmp.glob('*.png')}
        corpus_names = set(inputs)
        inputs.update({'mutation-' + k: v for k, v in cases().items()})
        jobs = []
        for name, data in inputs.items():
            path = tmp / (name + '.png')
            path.write_bytes(data)
            # BMP format corpus and all malformed cases, avoiding huge allocations
            # in the old unvalidated converter for malicious bit-depth/palette data.
            jobs.append((name, 'frame', [str(path), str(tmp / (name + '.pxc')), '6', '0']))
            if name == 'mutation-aspect-tall':
                jobs.extend((name, 'bmp' + str(mode), [str(path), str(mode)]) for mode in (4, 5))
            if name in corpus_names or name.startswith('mutation-format-') or name.startswith('mutation-filter-'):
                for mode in range(6):
                    jobs.append((name, 'bmp' + str(mode), [str(path), str(mode)]))
        def run(job):
            name, mode, argv = job
            binary = 'image_oracle' if mode == 'frame' else 'png_bmp_oracle'
            try:
                result = subprocess.run([str(args.build.resolve() / binary), *argv], capture_output=True, timeout=2)
                out = {'exit': result.returncode, 'bytes': len(result.stdout),
                       'sha256': hashlib.sha256(result.stdout).hexdigest()}
                if result.returncode == 0:
                    out['success'] = result.stdout[60] if mode == 'frame' else result.stdout[0]
            except subprocess.TimeoutExpired:
                out = {'timeout': True}
            return name + '/' + mode, out
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            actual = dict(pool.map(run, jobs))
        document = {'revision': 'da609bbfbb3f4a901823582fc0378aaf2ef3a55e', 'cases': actual,
                    'inputs': {k: hashlib.sha256(v).hexdigest() for k, v in inputs.items()}}
        packed = gzip.compress(json.dumps(document, sort_keys=True, separators=(',', ':')).encode(), mtime=0)
        if args.actual:
            args.actual.write_bytes(packed)
        if args.capture:
            args.output.write_bytes(packed)
        else:
            expected = json.loads(gzip.decompress(args.output.read_bytes()))
            assert expected['inputs'] == document['inputs'], 'input inventory changed'
            exceptions = json.loads((ROOT / 'png-rejections.json').read_text())
            bad = []
            for key, value in actual.items():
                before = expected['cases'][key]
                if key in exceptions:
                    approved = exceptions[key]
                    if approved['old'] != before or approved['expected'] != value or before == value:
                        bad.append(key)
                elif value != before:
                    bad.append(key)
            for k in bad[:20]:
                print(k, expected['cases'][k], actual[k])
            if bad:
                raise SystemExit(f'{len(bad)}/{len(actual)} PNG outcomes differ')
        print(f'{len(inputs)} PNG inputs, {len(actual)} consumer outcomes recorded/checked')
        print('timeouts:', sum('timeout' in v for v in actual.values()))


if __name__ == '__main__':
    main()
