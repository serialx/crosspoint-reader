#!/usr/bin/env python3
"""Compare complete UI captures, checking every pixel and refresh request."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops
from capture import HERE
from contracts import declared_cases


def read_capture(root, selected=None):
    manifest = json.loads((root / 'manifest.json').read_text())
    if manifest.get('schema') != 2:
        raise ValueError('Legacy recording requires explicit migrate.py validation')
    if manifest['status'] != 'passed':
        raise ValueError(f'Incomplete capture: {root}')
    screens = {}
    names = set()
    for run in manifest['runs']:
        if run['name'] in names:
            raise ValueError(f'Duplicate case: {run["name"]}')
        names.add(run['name'])
        if selected is not None and run['name'] not in selected:
            continue
        if run['exit_code']:
            raise ValueError(f'Failed run: {run["name"]}')
        if not run['screens'] or 'startup' not in run['screens'][0]:
            raise ValueError(f'Missing startup interval: {run["name"]}')
        for screen in run['screens']:
            if screen.get('activity_observed') != screen.get('activity_expected'):
                raise ValueError(f'Unexpected activity: {run["name"]}/{screen["id"]}')
            key = run['name'] + '/' + screen['id']
            if key in screens:
                raise ValueError(f'Duplicate screen: {key}')
            screens[key] = screen
    if selected is not None:
        expected = {name + '/' + s['id'] for name, (g, m, _, _) in selected.items()
                    for s in g['screens'] if m in s.get('machines', ['x3', 'x4'])}
        if set(screens) != expected:
            raise ValueError(f'Incomplete selected screen set: missing={sorted(expected - set(screens))}, '
                             f'extra={sorted(set(screens) - expected)}')
    if not screens:
        raise ValueError('No captured screens')
    return manifest, screens


def compare(old, new, selection=None):
    selected = declared_cases(json.loads((HERE / 'screens.json').read_text()), selection) if selection is not None else None
    a, aa = read_capture(old, selected)
    b, bb = read_capture(new, selected)
    for key in ['schema', 'suite_sha256', 'harness_sha256', 'xtc_fixture_sha256',
                'executable_identity', 'rtc', 'sleep', 'turbo', 'ghosting', 'input_quiet_ms', 'reader_input_quiet_ms']:
        if a.get(key) != b.get(key):
            raise ValueError(f'Incompatible capture setting: {key}')
    if set(aa) != set(bb):
        raise ValueError(f'Screen sets differ: missing={sorted(set(aa)-set(bb))}, extra={sorted(set(bb)-set(aa))}')
    left_runs = {r['name']: r for r in a['runs']}
    for run in b['runs']:
        if selected is not None and run['name'] not in selected:
            continue
        for field in ['input_sha256', 'script_sha256', 'contract']:
            if left_runs[run['name']].get(field) != run.get(field):
                raise ValueError(f'Input mismatch: {run["name"]}/{field}')
    results = []
    for key in sorted(aa):
        left, right = aa[key], bb[key]
        paths = [old / left['image'], new / right['image']]
        images = []
        for path, screen in zip(paths, [left, right]):
            if hashlib.sha256(path.read_bytes()).hexdigest() != screen['png_sha256']:
                raise ValueError(f'Image changed after capture: {path}')
            with Image.open(path) as image:
                images.append(image.convert('RGB'))
        if images[0].size != images[1].size:
            count, box = None, None
            pixels_equal = False
        else:
            diff = ImageChops.difference(*images)
            box = diff.getbbox()
            pixels_equal = box is None
            count = 0
            if box is not None:
                raw = diff.tobytes()
                count = sum(raw[i:i + 3] != b'\0\0\0' for i in range(0, len(raw), 3))
        refresh_equal = (left['refresh_requests'] == right['refresh_requests'] and
                         left.get('controller_refreshes') == right.get('controller_refreshes'))
        results.append({'screen': key, 'pixels_equal': pixels_equal, 'different_pixels': count,
                        'bbox': box, 'refresh_equal': refresh_equal,
                        'old_refresh': left['refresh_requests'], 'new_refresh': right['refresh_requests'],
                        'startup_equal': left.get('startup') == right.get('startup'),
                        'panel_count_equal': left['panel_refreshes'] == right['panel_refreshes']})
    passed = all(r['pixels_equal'] and r['refresh_equal'] and r['panel_count_equal'] and r['startup_equal'] for r in results)
    return {'passed': passed, 'screens': len(results), 'equal_pixels': sum(r['pixels_equal'] for r in results),
            'equal_refresh': sum(r['refresh_equal'] for r in results),
            'startup_intervals': sum('startup' in s for s in aa.values()),
            'equal_startup': sum(r['startup_equal'] for r in results if 'startup' in aa[r['screen']]),
            'selection': selection, 'results': results}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline', type=Path)
    p.add_argument('candidate', type=Path)
    p.add_argument('--json', type=Path)
    p.add_argument('--machines', nargs='+', choices=['x3', 'x4'])
    p.add_argument('--orientations', nargs='+', type=int, choices=range(4))
    p.add_argument('--dark', nargs='+', type=int, choices=[0, 1])
    p.add_argument('--groups', nargs='+')
    args = p.parse_args()
    selection = {k: getattr(args, k) for k in ['machines', 'orientations', 'dark', 'groups']
                 if getattr(args, k) is not None}
    result = compare(args.baseline, args.candidate, selection or None)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{result['equal_pixels']}/{result['screens']} pixel-identical; {result['equal_refresh']}/{result['screens']} refresh-identical")
    for row in result['results']:
        if not row['pixels_equal'] or not row['refresh_equal'] or not row['panel_count_equal'] or not row['startup_equal']:
            print(json.dumps(row))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
