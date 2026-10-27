#!/usr/bin/env python3
"""Freeze SHA-256 framebuffer and ordered selection-geometry records."""
import argparse
import gzip
import hashlib
import json
import pathlib
import subprocess
p = argparse.ArgumentParser()
p.add_argument('action', choices=['capture', 'check'])
p.add_argument('--binary', type=pathlib.Path, required=True)
p.add_argument('--work-dir', type=pathlib.Path, required=True)
p.add_argument('--golden', type=pathlib.Path, default=pathlib.Path(__file__).with_name('golden.json.gz'))
p.add_argument('--quick', action='store_true')
a = p.parse_args()
if a.action == 'capture':
    root = pathlib.Path(__file__).resolve().parents[2]
    changed = subprocess.check_output(['git', 'diff', 'da609bbf', '--', 'lib', 'src', 'rust', 'freeink-sdk'], cwd=root)
    if changed or subprocess.check_output(['git', '-C', str(root / 'freeink-sdk'), 'rev-parse', 'HEAD']).strip() != b'37d272d42732c31065ebed4d9cb90cef7bfa7416':
        raise SystemExit('Capture requires production da609bbf and pinned SDK')
a.work_dir.mkdir(parents=True, exist_ok=True)
frames, records = {}, []
with (a.work_dir / 'stderr.log').open('wb') as log:
    process = subprocess.Popen([str(a.binary.resolve())] + (['--quick'] if a.quick else []), stdout=subprocess.PIPE,
                               stderr=log, cwd=a.work_dir)
    while line := process.stdout.readline():
        if line.startswith(b'frame\t'):
            key = line.decode().strip().split('\t')[1]
            data = process.stdout.read(48000)
            if len(data) != 48000 or key in frames:
                raise SystemExit('Truncated or duplicate frame: ' + key)
            frames[key] = hashlib.sha256(data).hexdigest()
        else:
            records.append(line.decode())
    if process.wait():
        raise SystemExit(f'Harness failed: {process.returncode}; see {log.name}')
result = dict(frames=frames, geometry=records)
(a.work_dir / 'result.json').write_text(json.dumps(result, sort_keys=True))
if a.action == 'capture':
    if a.quick:
        raise SystemExit('Cannot capture a subset')
    with gzip.GzipFile(a.golden, 'wb', mtime=0) as out:
        out.write(json.dumps(result, sort_keys=True).encode())
else:
    expected = json.loads(gzip.decompress(a.golden.read_bytes()))
    if result != expected:
        differences = [k for k in frames if frames[k] != expected['frames'].get(k)]
        raise SystemExit(f'Oracle mismatch: {len(differences)} frames, geometry={records != expected["geometry"]}; {differences[:5]}')
print(f'{len(frames)} framebuffer SHA-256 values and {len(records)} geometry records: {a.action} passed')
