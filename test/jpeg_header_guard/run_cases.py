"""Run every JPEG header-guard case, one process per case and decoder copy.

Malformed cases: the shipped decoder rejects cleanly (no sanitizer report, JPEG_DECODE_ERROR) and the
observed copy reaches no unsafe use; the copy without the guard patch reaches the named unsafe use.
EXIF cases: the shipped decoder skips the bad block and decodes the reference pixels.
Valid, unusual and corpus files: the shipped decoder gives exactly the unguarded decoder's result,
including the number of file reads, bytes read and seeks.
"""
import argparse
import json
import os
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

FIELDS = ('open', 'error', 'decode_error', 'width', 'height', 'orientation', 'thumb', 'decode', 'hash', 'io')  # io: reads, bytes, seeks
SANITIZER = ('ERROR: AddressSanitizer', 'runtime error:', 'UndefinedBehaviorSanitizer', 'SUMMARY: ')
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:handle_segv=1', UBSAN_OPTIONS='print_stacktrace=1')


def run(binary, *args):
    try:
        p = subprocess.run([binary, *args], capture_output=True, text=True, errors='replace', timeout=30, env=ENV)
        rc, out, err = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired as e:
        rc, out, err = 'timeout', e.stdout or '', e.stderr or ''
        out, err = (x.decode(errors='replace') if isinstance(x, bytes) else x for x in (out, err))
    lines = [line for line in out.splitlines() if line.startswith('{')]
    result = json.loads(lines[-1]) if lines else None
    hits = sorted({line.split()[1] for line in err.splitlines() if line.startswith('JHG_HIT ')})
    sanitizer = [line.strip() for line in err.splitlines() if any(s in line for s in SANITIZER)][:3]
    return dict(rc=rc, result=result, hits=hits, sanitizer=sanitizer)


def fields(r):
    return {k: r['result'][k] for k in FIELDS} if r['result'] else None


def clean(r):
    return r['rc'] == 0 and r['result'] is not None and not r['sanitizer']


def control_outcome(r):
    if r['rc'] == 'timeout':
        return 'hang (killed after 30 s)'
    if r['sanitizer'] or r['rc'] != 0:
        return 'sanitizer report or crash'
    if r['result']['open'] == 1:
        return 'malformed header accepted'
    return 'rejected only after the unsafe use'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--guarded', required=True)
    ap.add_argument('--guarded-observed', required=True)
    ap.add_argument('--control', required=True)
    ap.add_argument('--corpus', nargs='*', default=[])
    ap.add_argument('--report', required=True)
    a = ap.parse_args()
    decode_error = int(subprocess.check_output([a.guarded, '--decode-error'], text=True))
    cases = json.loads(subprocess.check_output([a.guarded, '--list'], text=True))
    failures, rows, guarded_hash = [], [], {}

    def check(ok, what):
        if not ok:
            failures.append(what)
        return ok

    for case in cases:
        name, kind = case['name'], case['kind']
        g, go, c = (run(b, '--case', name) for b in (a.guarded, a.guarded_observed, a.control))
        row = dict(case, guarded=g, guarded_observed=go, control=c)
        rows.append(row)
        check(clean(g) and clean(go), f'{name}: shipped decoder not clean: {g["rc"]} {g["sanitizer"]} {go["sanitizer"]}')
        check(not go['hits'], f'{name}: guarded copy reached {go["hits"]}')
        if not (g['result'] and go['result']):
            continue
        check(fields(g) == fields(go), f'{name}: observer changed the result')
        guarded_hash[name] = g['result']['hash']
        if kind in ('malformed', 'exif'):
            expected = case['control'].split(',')
            check(set(expected) <= set(c['hits']), f'{name}: control reached {c["hits"]}, expected {expected}')
            row['control_outcome'] = control_outcome(c)
        if kind == 'malformed':
            check(g['result']['open'] == 0 and g['result']['error'] == decode_error,
                  f'{name}: not rejected with JPEG_DECODE_ERROR: {fields(g)}')
        elif kind == 'exif':
            check(g['result']['open'] == 1 and g['result']['decode'] == 1, f'{name}: bad EXIF not skipped: {fields(g)}')
        elif kind == 'unusual' and fields(g) != fields(c):
            # Allowed difference: the unguarded decoder "opened" a header with no usable frame.
            zero = c['result'] and c['result']['open'] == 1 and not (c['result']['width'] and c['result']['height'])
            check(zero and g['result']['open'] == 0, f'{name}: differs from unguarded decoder: {fields(g)} vs {fields(c)}')
            row['difference'] = 'unguarded decoder opened a 0x0 frame; shipped decoder rejects'
        else:
            check(fields(g) == fields(c), f'{name}: differs from unguarded decoder: {fields(g)} vs {fields(c)}')
            if kind == 'valid':
                check(g['result']['open'] == 1 and g['result']['decode'] == 1, f'{name}: valid file failed: {fields(g)}')
                check(not c['hits'], f'{name}: unguarded decoder reached {c["hits"]} on a valid file')

    for row in rows:
        if row['kind'] == 'exif' and row['guarded']['result']:
            check(row['guarded']['result']['hash'] == guarded_hash.get(row['reference']),
                  f'{row["name"]}: pixels differ from {row["reference"]}')

    corpus = []
    with tempfile.TemporaryDirectory() as tmp:
        for archive in a.corpus:
            with tarfile.open(archive) as tar:
                for member in tar.getmembers():
                    if not member.isfile() or not member.name.lower().endswith(('.jpg', '.jpeg')):
                        continue
                    path = Path(tmp) / f'{len(corpus)}.jpg'
                    path.write_bytes(tar.extractfile(member).read())
                    g, go, c = (run(b, '--file', str(path)) for b in (a.guarded, a.guarded_observed, a.control))
                    label = f'{Path(archive).name}:{member.name}'
                    corpus.append(dict(file=label, guarded=fields(g), control=fields(c), control_hits=c['hits']))
                    check(clean(g) and clean(go) and not go['hits'], f'{label}: shipped decoder not clean')
                    check(fields(g) == fields(c), f'{label}: differs: {fields(g)} vs {fields(c)}')

    kinds = {}
    for row in rows:
        kinds[row['kind']] = kinds.get(row['kind'], 0) + 1
    summary = dict(cases=len(rows), kinds=kinds, corpus_files=len(corpus),
                   corpus_opened=sum(1 for r in corpus if r['guarded'] and r['guarded']['open'] == 1),
                   control_outcomes={r['name']: r['control_outcome'] for r in rows if 'control_outcome' in r},
                   failures=failures)
    Path(a.report).write_text(json.dumps(dict(summary=summary, cases=rows, corpus=corpus), indent=1) + '\n')
    for name, outcome in summary['control_outcomes'].items():
        hits = next(r['control']['hits'] for r in rows if r['name'] == name)
        print(f'control {name}: {outcome}; reached {",".join(hits)}')
    print(f'{len(rows)} cases {kinds}; {len(corpus)} corpus files ({summary["corpus_opened"]} open); '
          f'{len(failures)} failures')
    for f in failures:
        print('FAIL', f)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
