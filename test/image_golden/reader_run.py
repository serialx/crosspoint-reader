#!/usr/bin/env python3
"""Check the reader binding against immutable variant-2 pixels and cache bytes."""
import concurrent.futures
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

from pipeline_diagnostics import selected

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('image_run', ROOT / 'run.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def main():
    binary = Path(sys.argv[1]).resolve()
    golden = json.loads(gzip.decompress((ROOT / 'goldens.json.gz').read_bytes()))
    expected = {key: value for key, value in golden['cases'].items() if key.split('/')[1] == '2'}
    assert len(expected) == 4608
    pipeline = json.loads(gzip.decompress((ROOT / 'pipeline-diagnostic-goldens.json.gz').read_bytes()))
    with tempfile.TemporaryDirectory(prefix='reader-image-golden-') as temporary:
        directory = Path(temporary)
        with tarfile.open(ROOT / 'corpus.tar.gz') as archive:
            archive.extractall(directory, filter='data')
        assert {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.iterdir()} == golden['corpus']

        def run(key):
            name, variant, target = key.split('/')
            path = directory / name
            cache = directory / (name + '-' + target + '.px2')
            result = subprocess.run(
                [str(binary), str(path), str(cache), variant, target],
                capture_output=True, check=True, env=dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1'))
            actual = oracle.unpack(result.stdout)
            fields = ['frame', 'cache_exists', 'cache', 'cache_bytes']
            observed = {field: actual[field] for field in fields}
            wanted = {field: expected[key][field] for field in fields}
            observed['success'] = actual['status'][15]
            wanted['success'] = expected[key]['status'][15]
            if target == '0':
                observed['milestones'] = [line.replace(str(cache), '<cache>').replace(str(path), '<input>')
                                          for line in selected(result.stderr.decode().splitlines())]
                wanted['milestones'] = pipeline['cases'][name + '/' + variant]['logs']
            return key, observed, wanted

        failures, counts = [], {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            for key, observed, wanted in pool.map(run, expected):
                extension = Path(key.split('/')[0]).suffix
                counts[extension] = counts.get(extension, 0) + 1
                if observed != wanted:
                    failures.append(dict(case=key, expected=wanted, actual=observed))
    report = dict(cases=len(expected), formats=counts, failures=failures, diagnostic_cases=96,
                  golden_sha256=hashlib.sha256((ROOT / 'goldens.json.gz').read_bytes()).hexdigest(),
                  diagnostic_sha256=hashlib.sha256((ROOT / 'pipeline-diagnostic-goldens.json.gz').read_bytes()).hexdigest(),
                  note='Renderer query counts differ because the reader lends a target snapshot; success, pixels, '
                       'cache bytes/length/existence and frozen pipeline milestones are compared.')
    binary.parent.joinpath('reader-oracle-results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(cases=len(expected), formats=counts, failures=len(failures))))
    if failures:
        print(json.dumps(failures[:3], indent=2))
        raise SystemExit(1)


if __name__ == '__main__':
    main()
