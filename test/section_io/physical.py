#!/usr/bin/env python3
"""Fault every added Book/metadata/ZIP operation without a logical trace projection."""
import argparse
import base64
from collections import Counter
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time

from actual_owner import run as run_owner
from run import synthetic

MODES = ('full', 'suspend', 'resume', 'abandon')


def execute(binary, mode, fault, source):
    with tempfile.TemporaryDirectory(prefix='section-physical-fault-') as directory:
        root = Path(directory)
        (root / 'book').mkdir()
        (root / 'book/chapter.xhtml').write_bytes(source)
        process = subprocess.run([str(binary), mode, str(fault), '--physical'], cwd=root,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        files = {str(path.relative_to(root)): base64.b64encode(path.read_bytes()).decode()
                 for path in sorted((root / 'cache').rglob('*')) if path.is_file()}
        return {'status': process.returncode, 'trace': process.stdout.decode(errors='replace'), 'files': files}


def validate(result, baseline, fault):
    if result['status']:
        raise ValueError(f"physical producer status {result['status']}")
    trace = result['trace']
    cleanup = re.findall(r'^cleanup rust (\d+) (\d+) native (\d+) (\d+) buffers (\d+) files (\d+) '
                         r'cache-held (\d+) framebuffer (\d+)$', trace, re.M)
    if len(cleanup) != 1:
        raise ValueError('missing cleanup result')
    rust_before, rust_after, native_before, native_after, buffers, files, lock, framebuffer = map(int, cleanup[0])
    if (rust_before != rust_after or native_before != native_after or buffers or files or lock or framebuffer != 1):
        raise ValueError(f'resource cleanup mismatch: {cleanup[0]}')
    outcome = re.findall(r'^outcome (\d+)$', trace, re.M)
    admissions = re.findall(r'^admission (first|second) ([01])$', trace, re.M)
    expected = {'0': [('first', '1'), ('second', '1')], '10': [('first', '0')],
                '11': [('first', '1'), ('second', '0')]}
    if len(outcome) != 1 or admissions != expected.get(outcome[0]):
        raise ValueError('admission result does not match completed owner scopes')
    operations = [line for line in trace.splitlines() if line.startswith('io ')]
    original = [line for line in baseline.splitlines() if line.startswith('io ')]
    if fault:
        if len(operations) < fault or operations[:fault - 1] != original[:fault - 1]:
            raise ValueError('physical prefix changed before injected operation')
        if operations[fault - 1] != original[fault - 1] + ' FAIL':
            raise ValueError('fault did not hit the approved physical operation')
        if sum(line.endswith(' FAIL') for line in operations) != 1:
            raise ValueError('expected exactly one injected failure')
    else:
        without_results = '\n'.join(line for line in trace.splitlines()
                                    if not line.startswith(('admission ', 'outcome ', 'cleanup '))) + '\n'
        if without_results != baseline:
            raise ValueError('unfaulted physical harness changed the approved baseline')
    return int(outcome[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--output', type=Path, help='New evidence directory; defaults to a unique directory beside the binary')
    args = parser.parse_args()
    binary = args.oracle.resolve()
    if args.output is None:
        args.output = binary.parent / f'section-physical-{time.time_ns()}'
    args.output.mkdir(parents=True, exist_ok=False)
    approved = json.loads(gzip.decompress((Path(__file__).parent / 'physical_baselines.json.gz').read_bytes()))
    source = synthetic()
    baselines, schedule = {}, []
    for mode in MODES:
        baseline = run_owner(binary, mode, source)
        trace = baseline['physical_trace']
        if hashlib.sha256(trace.encode()).hexdigest() != approved[mode]['sha256']:
            raise ValueError(f'{mode}: physical baseline differs from accepted mapping')
        extra = [row for row in baseline['mapping'] if 'extra' in row]
        if extra != approved[mode]['extra']:
            raise ValueError(f'{mode}: additional operation inventory differs')
        baselines[mode] = trace
        schedule.append((mode, None))
        schedule.extend((mode, row) for row in extra)
    assert len(schedule) == 5260
    (args.output / 'schedule.json').write_text(json.dumps(schedule, indent=2) + '\n')
    outcomes = Counter()
    start = time.monotonic()
    with gzip.open(args.output / 'records.jsonl.gz', 'wt', compresslevel=1) as capture:
        for index, (mode, operation) in enumerate(schedule):
            fault = operation['physical'] if operation else 0
            result = execute(binary, mode, fault, source)
            record = {'mode': mode, 'operation': operation, 'result': result}
            capture.write(json.dumps(record, separators=(',', ':')) + '\n')
            try:
                outcome = validate(result, baselines[mode], fault)
            except ValueError as error:
                (args.output / 'failure.json').write_text(json.dumps(record, indent=2) + '\n')
                raise SystemExit(f'{mode} physical {fault}: {error}') from error
            category = operation['extra'] if operation else 'unfaulted'
            outcomes[f'{category}/outcome-{outcome}'] += 1
            if index % 100 == 0:
                print(f'{index + 1}/{len(schedule)} passed', flush=True)
    summary = {'cases': len(schedule), 'faults': len(schedule) - 4, 'status': 'passed',
               'outcomes': dict(sorted(outcomes.items())), 'seconds': time.monotonic() - start,
               'capture': str(args.output)}
    (args.output / 'results.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary), flush=True)


if __name__ == '__main__':
    main()
