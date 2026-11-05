#!/usr/bin/env python3
"""Replay every frozen GIF framebuffer variant without external decoder sources."""
import concurrent.futures
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'image_golden'))
from run import digest, unpack
root = Path(__file__).resolve().parents[1] / 'image_golden'
golden = json.loads(gzip.decompress((root / 'goldens.json.gz').read_bytes()))
with tempfile.TemporaryDirectory(prefix='gif-matrix-') as directory:
    directory = Path(directory)
    with tarfile.open(root / 'corpus.tar.gz') as archive:
        for member in archive.getmembers():
            if member.name.endswith('.gif'):
                data = archive.extractfile(member).read()
                assert digest(data) == golden['corpus'][member.name]
                (directory / member.name).write_bytes(data)
    cases = {k:v for k,v in golden['cases'].items() if k.split('/')[0].endswith('.gif')}
    def run(item):
        key, expected = item
        name, variant, target = key.split('/')
        cache = directory / (key.replace('/', '-') + '.pxc')
        actual = unpack(subprocess.check_output([str(Path(sys.argv[1]).resolve()), str(directory/name), str(cache), variant, target]))
        if actual != expected:
            return key
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        failures = [k for k in pool.map(run, cases.items()) if k]
    if failures:
        raise SystemExit(f'{len(failures)} GIF matrix differences: {failures[:12]}')
    print(f'{len(cases)} GIF matrix cases passed')
