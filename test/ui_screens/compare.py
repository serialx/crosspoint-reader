#!/usr/bin/env python3
"""Compare complete UI captures, checking every pixel and refresh request."""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image, ImageChops


def read_capture(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    if manifest['status'] != 'passed':
        raise ValueError(f'Incomplete capture: {root}')
    screens = {}
    for run in manifest['runs']:
        if run['exit_code']:
            raise ValueError(f'Failed run: {run["name"]}')
        for screen in run['screens']:
            if screen.get('activity_observed') != screen.get('activity_expected'):
                raise ValueError(f'Unexpected activity: {run["name"]}/{screen["id"]}')
            key = run['name'] + '/' + screen['id']
            if key in screens:
                raise ValueError(f'Duplicate screen: {key}')
            screens[key] = screen
    if not screens:
        raise ValueError('No captured screens')
    return manifest, screens


def compare(old, new):
    a, aa = read_capture(old)
    b, bb = read_capture(new)
    for key in ['schema', 'suite_sha256', 'harness_sha256', 'xtc_fixture_sha256',
                'emulator', 'rtc', 'sleep', 'turbo', 'ghosting', 'input_quiet_ms', 'reader_input_quiet_ms']:
        if a.get(key) != b.get(key):
            raise ValueError(f'Incompatible capture setting: {key}')
    if set(aa) != set(bb):
        raise ValueError(f'Screen sets differ: missing={sorted(set(aa)-set(bb))}, extra={sorted(set(bb)-set(aa))}')
    left_runs = {r['name']: r for r in a['runs']}
    for run in b['runs']:
        for field in ['input_sha256', 'script_sha256']:
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
                        'panel_count_equal': left['panel_refreshes'] == right['panel_refreshes']})
    passed = all(r['pixels_equal'] and r['refresh_equal'] and r['panel_count_equal'] for r in results)
    return {'passed': passed, 'screens': len(results), 'equal_pixels': sum(r['pixels_equal'] for r in results),
            'equal_refresh': sum(r['refresh_equal'] for r in results), 'results': results}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline', type=Path)
    p.add_argument('candidate', type=Path)
    p.add_argument('--json', type=Path)
    args = p.parse_args()
    result = compare(args.baseline, args.candidate)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{result['equal_pixels']}/{result['screens']} pixel-identical; {result['equal_refresh']}/{result['screens']} refresh-identical")
    for row in result['results']:
        if not row['pixels_equal'] or not row['refresh_equal'] or not row['panel_count_equal']:
            print(json.dumps(row))
    raise SystemExit(0 if result['passed'] else 1)


if __name__ == '__main__':
    main()
