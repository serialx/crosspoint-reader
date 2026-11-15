#!/usr/bin/env python3
"""Compare original serial diagnostics using the frozen consumer build targets."""
import argparse
import concurrent.futures
import importlib.util
import json
import gzip
import hashlib
import os
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
from png_cases import cases, bmp_chunk_case
from pipeline_diagnostics import selected

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('gif_oracle', HERE.parent / 'gif_support/oracle.py')
gif = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gif)

def normalize(line):
    # Decoder status integers disappeared with PNGdec/AnimatedGIF.
    line = re.sub(r'(Failed to open PNG(?: for dimensions)?|Decode failed): -?\d+$', r'\1', line)
    line = re.sub(r'GIF decode failed \(state=-?\d+, err=-?\d+\)$', 'GIF decode failed', line)
    line = re.sub(r'Failed to open GIF \(err=-?\d+\)$', 'Failed to open GIF', line)
    line = re.sub(r'AnimatedGIF open failed: err=-?\d+ ', 'AnimatedGIF open failed: ', line)
    line = re.sub(r'(?:/[^ \'=]+)*/image-diagnostics-[^/]+/[^ \'\n]+', '<input>', line)
    return line

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--capture', type=Path)
    parser.add_argument('--goldens', type=Path)
    parser.add_argument('--exceptions', type=Path)
    args = parser.parse_args()
    frozen = json.loads(gzip.decompress(args.goldens.read_bytes())) if args.goldens else None
    exceptions = json.loads(args.exceptions.read_text())['cases'] if args.exceptions else {}
    build = args.build.resolve()
    env = dict(os.environ, IMAGE_DIAGNOSTIC_LOGS='1')
    with tempfile.TemporaryDirectory(prefix='image-diagnostics-') as tmp:
        root = Path(tmp)
        corpus = {('png', k): v for k, v in cases().items()}
        corpus.update({('gif', k): v for k, v in gif.fixtures().items()})
        with tarfile.open(HERE / 'corpus.tar.gz') as archive:
            for member in archive.getmembers():
                if member.name.endswith('.png'):
                    corpus['png', member.name] = archive.extractfile(member).read()
        jobs = []
        for (kind, name), data in corpus.items():
            path = root / f'{kind}-{name}.{kind}'
            path.write_bytes(data)
            jobs.append((kind, name, 'frame', [str(path), str(path)+'.pxc', '6', '0']))
            # Match the established bounded BMP mutation schedule.
            if kind == 'gif' or name.endswith('.png') or name.startswith(('format-', 'filter-')) or bmp_chunk_case(name):
                jobs.append((kind, name, 'bmp', [str(path), '4']))
            if kind == 'gif' and name == 'small':
                for label, limit in [('write-header', 0), ('write-row', 63)]:
                    jobs.append((kind, name, label, [str(path), '4', str(limit)]))
        def run(job):
            kind, name, mode, argv = job
            binaries = ['diagnostic_frame_reference', 'image_oracle'] if mode == 'frame' else [f'diagnostic_{kind}_reference', f'{kind}_bmp_oracle' if kind == 'png' else 'gif_oracle']
            logs, status = [], []
            key = f'{kind}/{name}/{mode}'
            for variant, binary in enumerate(binaries):
                if variant == 0 and frozen is not None:
                    logs.append(frozen['cases'][key]['logs'])
                    status.append(frozen['cases'][key]['exit'])
                    continue
                try:
                    result = subprocess.run([str(build / binary), *argv], env=env, capture_output=True, timeout=3)
                    logs.append(sorted(normalize(s) for s in result.stderr.decode(errors='replace').splitlines()
                                       if not selected([s])))
                    status.append(result.returncode)
                except subprocess.TimeoutExpired:
                    logs.append(['TIMEOUT'])
                    status.append(None)
            return key, {'reference': logs[0], 'candidate': logs[1], 'exit': status}
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            results = {}
            for index, (key, value) in enumerate(pool.map(run, jobs)):
                results[key] = value
                if index % 500 == 0:
                    print(f'{index}/{len(jobs)} cases', flush=True)
        hashes = {f'{kind}/{name}': hashlib.sha256(data).hexdigest() for (kind, name), data in corpus.items()}
        if frozen is not None:
            assert hashes == frozen['inputs'], 'diagnostic fixture inventory changed'
            assert results.keys() == frozen['cases'].keys(), 'diagnostic case inventory changed'
        if args.capture:
            document = {'revision': 'da609bbf', 'inputs': hashes,
                        'cases': {k: {'logs': v['reference'], 'exit': v['exit'][0]} for k, v in results.items()}}
            args.capture.write_bytes(gzip.compress(json.dumps(document, sort_keys=True).encode(), mtime=0))
        differences = {k: v for k, v in results.items() if v['reference'] != v['candidate']}
        unexpected = {k: v for k, v in differences.items() if k not in exceptions or v['candidate'] != exceptions[k]['logs']}
        stale = sorted(set(exceptions) - set(differences))
        args.report.write_text(json.dumps({'cases': len(results), 'matched': len(results)-len(differences), 'differences': differences, 'unexpected': unexpected, 'stale': stale}, indent=2))
        print(f'{len(results)-len(differences)}/{len(results)} diagnostic multisets identical; {len(differences)} differences')
        print(f'{len(unexpected)} unexplained differences; {len(stale)} stale exceptions')
        return bool(unexpected or stale)

if __name__ == '__main__':
    raise SystemExit(main())
