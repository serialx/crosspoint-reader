#!/usr/bin/env python3
"""Replay the immutable legacy logical Section trace through the actual Rust owners."""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
from actual_owner import run as run_owner

def synthetic():
    paragraphs = ''.join(f'<p id="p{i}">Paragraph {i}: Alpha beta gamma delta epsilon zeta eta theta.</p>' for i in range(36))
    return ('<html><body><h1 id="start">Start</h1>' + paragraphs +
            '<h2 id="middle">Middle</h2><ol><li>One</li><li>Two</li></ol>' +
            '<p id="end">The end.</p></body></html>').encode()


def fingerprint(result):
    digest = lambda data: hashlib.sha256(data).hexdigest()
    return {'status': result['status'], 'trace': digest(result['trace'].encode()),
            'files': {name: digest(base64.b64decode(data)) for name, data in result['files'].items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--golden', type=Path, default=Path(__file__).parent / 'golden.json.gz')
    parser.add_argument('--record', action='store_true')
    parser.add_argument('--capture', type=Path, help='Full physical/logical JSONL records; defaults beside the binary')
    args = parser.parse_args()
    if args.record:
        parser.error('The historical golden is immutable; use --capture to retain actual-owner records')
    binary = args.oracle.resolve()
    cases = []
    baselines = {}
    steps = 0
    for mode in ('full', 'suspend', 'resume', 'abandon'):
        normal = run_owner(binary, mode, synthetic())
        baselines[mode] = normal
        count = int(re.search(r'steps (\d+)', normal['trace'])[1])
        steps += count
        cases.append((mode, mode, 0))
        cases += [(f'{mode}-fault-{i}', mode, i) for i in range(1, count + 1)]
    with gzip.open(args.golden, 'rt') as file:
        expected = json.load(file)
    assert set(expected) == {key for key, _, _ in cases}, 'case inventory differs'
    capture = args.capture or binary.parent / 'section-io-physical.jsonl.gz'
    capture.parent.mkdir(parents=True, exist_ok=True)
    with gzip.open(capture, 'wt', compresslevel=1) as records:
        for key, mode, fault in cases:
            actual = (run_owner(binary, mode, synthetic(), fault, baselines[mode]['mapping'])
                      if fault else baselines[mode])
            # The deterministic projector reconstructs fault-case maps from the full raw trace.
            saved = {name: value for name, value in actual.items() if not fault or name != 'mapping'}
            records.write(json.dumps({'case': key, 'result': saved}, separators=(',', ':')) + '\n')
            if fingerprint(actual) != expected[key]:
                Path('/tmp/section-io-actual.json').write_text(json.dumps(actual, indent=2))
                Path('/tmp/section-io-expected.json').write_text(json.dumps(expected[key], indent=2))
                raise SystemExit(f'mismatch: {key}; details /tmp/section-io-{{actual,expected}}.json')
    print(f'PASS {len(cases)} legacy logical cases; {steps} mapped physical operation faults; raw records {capture}')


if __name__ == '__main__':
    main()
