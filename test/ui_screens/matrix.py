#!/usr/bin/env python3
"""Capture independent machine/polarity shards and combine their manifests."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--emulator-identity', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--jobs', type=int, choices=range(1, 5), default=4)
    args = p.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    start = time.monotonic()

    def capture(pair):
        machine, dark = pair
        name = f'{machine}-d{dark}'
        command = [sys.executable, str(HERE / 'capture.py'), '--build', str(args.build.resolve()),
                   '--emulator-identity', str(args.emulator_identity.resolve()), '--out', str(out / name),
                   '--machines', machine, '--dark', str(dark)]
        with (out / (name + '.log')).open('w') as log:
            code = subprocess.run(command, stdout=log, stderr=log).returncode
        print(f'{name}: {code}', flush=True)
        return name, code

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        outcomes = list(pool.map(capture, [(m, d) for m in ['x3', 'x4'] for d in [0, 1]]))
    combined = None
    for name, code in outcomes:
        path = out / name / 'manifest.json'
        if not path.exists():
            raise RuntimeError(f'{name} failed before manifest: see {out / (name + ".log")}')
        part = json.loads(path.read_text())
        if combined is None:
            combined = {k: v for k, v in part.items() if k not in ['runs', 'error', 'host_seconds', 'status']}
            combined.update(runs=[], status='passed', shards=[])
        combined['shards'].append({'name': name, 'status': part['status'], 'exit_code': code})
        if code or part['status'] != 'passed':
            combined['status'] = 'failed'
        for run in part['runs']:
            run['artifact_dir'] = name + '/' + run['artifact_dir']
            for screen in run['screens']:
                screen['image'] = name + '/' + screen['image']
            combined['runs'].append(run)
    combined['host_seconds'] = round(time.monotonic() - start, 3)
    combined['sum_run_host_seconds'] = round(sum(r['host_seconds'] for r in combined['runs']), 3)
    (out / 'manifest.json').write_text(json.dumps(combined, indent=2) + '\n')
    raise SystemExit(0 if combined['status'] == 'passed' else 1)


if __name__ == '__main__':
    main()
