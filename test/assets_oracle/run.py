#!/usr/bin/env python3
"""Replay production asset output against pre-port hashes, including short writes."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile

HERE = Path(__file__).resolve().parent

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--capture', action='store_true')
    args = parser.parse_args()
    binary = args.binary.resolve()
    results = {}
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        with tarfile.open(HERE.parent / 'image_golden/corpus.tar.gz') as archive:
            archive.extractall(root, filter='data')
        inputs = sorted(root.glob('*.jpg'))
        for source in inputs[:]:
            if '37x29' not in source.name:
                continue
            data = source.read_bytes()
            for length in [0, 2, 20, len(data)//2, len(data)-2]:
                path = root / f'{source.stem}-truncated-{length}.jpg'
                path.write_bytes(data[:length])
                inputs.append(path)
        def run(key, *argv):
            value = subprocess.run([str(binary), *map(str, argv)], capture_output=True,
                                   env={**os.environ, 'IMAGE_DIAGNOSTIC_LOGS': '1'}, timeout=20)
            if value.returncode:
                raise RuntimeError((key, value.returncode, value.stderr.decode(errors='replace')))
            results[key] = {'bytes':len(value.stdout), 'sha256':hashlib.sha256(value.stdout).hexdigest(),
                            'logs':value.stderr.decode(errors='replace')}
        for source in inputs:
            for variant in range(10):
                for limit in [2**64-1, 0, 17, 70]:
                    run(f'jpeg/{source.name}/{variant}/{limit}', 'jpeg', source, variant, limit)
        for w,h in [(792,528),(800,480),(768,552),(8,1),(16,9),(24,31)]:
            run(f'screenshot/{w}/{h}', 'screenshot', root/'screenshot.bmp',w,h)
        for i,text in enumerate(['', 'https://crosspointreader.com', 'WIFI:T:WPA;S:Reader;P:hello;;',
                                 '한글 UTF-8 日本語', *['x'*n for n in [114,115,395,396,1066,1067,2110,2111,2953,2954]],
                                 'x'*2952+'한글']):
            path = root/'qr.txt'
            path.write_text(text)
            for bounds in [(3,7,200,240),(0,0,1,1),(-7,13,527,792)]:
                run(f'qr/{i}/{bounds}', 'qr',path,*bounds)
    golden = HERE/'goldens.json.gz'
    if args.capture:
        with gzip.open(golden,'wt') as stream:
            json.dump(results,stream,sort_keys=True)
    else:
        with gzip.open(golden,'rt') as stream:
            expected=json.load(stream)
        errors=[key for key in sorted(expected.keys()|results.keys()) if expected.get(key)!=results.get(key)]
        if errors:
            for key in errors[:20]:
                print(key, '\nold:',expected.get(key),'\nnew:',results.get(key))
            raise SystemExit(f'{len(errors)} differing cases')
    print(f'{len(results)} asset cases passed')

if __name__ == '__main__':
    main()
