#!/usr/bin/env python3
"""Capture/replay Section lifecycle results, HAL traces, logs, and persisted bytes."""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

def synthetic():
    paragraphs = ''.join(f'<p id="p{i}">Paragraph {i}: Alpha beta gamma delta epsilon zeta eta theta.</p>' for i in range(36))
    return ('<html><body><h1 id="start">Start</h1>' + paragraphs +
            '<h2 id="middle">Middle</h2><ol><li>One</li><li>Two</li></ol>' +
            '<p id="end">The end.</p></body></html>').encode()


def run(binary, mode, fail_at=0):
    with tempfile.TemporaryDirectory(prefix='section-io-') as directory:
        root = Path(directory)
        (root / 'book').mkdir()
        (root / 'book/chapter.xhtml').write_bytes(synthetic())
        result = subprocess.run([str(binary), mode, str(fail_at)], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        files = {str(p.relative_to(root)): base64.b64encode(p.read_bytes()).decode()
                 for p in sorted((root / 'cache').rglob('*')) if p.is_file()}
        return {'status': result.returncode, 'trace': result.stdout.decode(errors='replace'), 'files': files}


def fingerprint(result):
    digest = lambda data: hashlib.sha256(data).hexdigest()
    return {'status': result['status'], 'trace': digest(result['trace'].encode()),
            'files': {name: digest(base64.b64decode(data)) for name, data in result['files'].items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--golden', type=Path, default=Path(__file__).parent / 'golden.json.gz')
    parser.add_argument('--record', action='store_true')
    parser.add_argument('--capture', type=Path, help='Optional full baseline records, outside the repository')
    args = parser.parse_args()
    binary = args.oracle.resolve()
    cases = []
    steps = 0
    for mode in ('full', 'suspend', 'resume', 'abandon'):
        normal = run(binary, mode)
        count = int(re.search(r'steps (\d+)', normal['trace'])[1])
        steps += count
        cases.append((mode, mode, 0))
        cases += [(f'{mode}-fault-{i}', mode, i) for i in range(1, count + 1)]
    if args.record:
        records = {key: run(binary, value, fault) for key, value, fault in cases}
        if args.capture:
            with gzip.open(args.capture, 'wt') as file:
                json.dump(records, file, sort_keys=True)
        actual = {key: fingerprint(value) for key, value in records.items()}
        args.golden.parent.mkdir(parents=True, exist_ok=True)
        with gzip.open(args.golden, 'wt') as file:
            json.dump(actual, file, sort_keys=True)
        print(f'recorded {len(actual)} cases; {steps} individually failed HAL operations')
        return
    with gzip.open(args.golden, 'rt') as file:
        expected = json.load(file)
    assert set(expected) == {key for key, _, _ in cases}, 'case inventory differs'
    for key, value, fault in cases:
        actual = run(binary, value, fault)
        if fingerprint(actual) != expected[key]:
            Path('/tmp/section-io-actual.json').write_text(json.dumps(actual, indent=2))
            Path('/tmp/section-io-expected.json').write_text(json.dumps(expected[key], indent=2))
            raise SystemExit(f'mismatch: {key}; details /tmp/section-io-{{actual,expected}}.json')
    print(f'PASS {len(cases)} cases; {steps} individually failed HAL operations')


if __name__ == '__main__':
    main()
