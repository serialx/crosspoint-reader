#!/usr/bin/env python3
"""Assemble a complete matrix from independently recorded, verified cases."""
import argparse
import copy
import json
import os
from pathlib import Path

from capture import HERE, save, sha, steps_for, summarize


def assemble(sources, out, allow_partial=False):
    if out.exists():
        raise ValueError(f'Output already exists: {out}')
    suite = json.loads((HERE / 'screens.json').read_text())
    expected = {}
    for machine in ['x3', 'x4']:
        for orientation in range(4):
            for dark in [0, 1]:
                for group in suite['groups']:
                    if machine not in group.get('machines', ['x3', 'x4']) or (orientation and not group.get('rotates')):
                        continue
                    expected[f'{machine}-o{orientation}-d{dark}-{group["id"]}'] = (group, machine, orientation, dark)
    chosen, recordings, replaced = {}, [], []
    common = ['schema', 'emulator', 'artifacts', 'rtc', 'sleep', 'turbo', 'ghosting', 'input_quiet_ms',
              'xtc_fixture_sha256']
    reference = None
    for root in sources:
        path = root / 'manifest.json'
        meta = json.loads(path.read_text())
        if meta['status'] != 'passed' and not (allow_partial and meta['status'] == 'failed'):
            raise ValueError(f'Unfinished or unapproved partial recording: {root}')
        if reference is None:
            reference = meta
        for key in common:
            if meta[key] != reference[key]:
                raise ValueError(f'Incompatible source: {root}/{key}')
        for name in ['fixtures.py', 'fault.py']:
            if meta['harness_sha256'][name] != sha(HERE / name):
                raise ValueError(f'Fixture/fault code changed: {name}')
        if meta['xtc_fixture_sha256'] != sha(HERE.parent / 'xtc_diff/generate.py'):
            raise ValueError('XTC fixture code changed')
        end = path.stat().st_mtime
        recordings.append({'manifest': str(path), 'sha256': sha(path), 'status': meta['status'],
                           'harness_sha256': meta['harness_sha256'], 'host_seconds': meta['host_seconds'],
                           'start_epoch': end - meta['host_seconds'], 'end_epoch': end})
        for run in meta['runs']:
            name = run['name']
            if name not in expected:
                raise ValueError(f'Undeclared case: {name}')
            if name in chosen:
                replaced.append({'case': name, 'old_source': str(chosen[name][0]), 'new_source': str(root)})
            chosen[name] = (root, copy.deepcopy(run))
    if set(chosen) != set(expected):
        raise ValueError(f'Missing cases: {sorted(set(expected) - set(chosen))}')
    runs = []
    for name, (group, machine, orientation, dark) in expected.items():
        root, run = chosen[name]
        if run['exit_code']:
            raise ValueError(f'Failed selected case: {name}')
        if (run['machine'], run['orientation'], run['dark'], run['group']) != (machine, orientation, dark, group['id']):
            raise ValueError(f'Case identity mismatch: {name}')
        case = Path(run['command'][run['command'].index('--out') + 1]).parent.resolve()
        if not case.is_relative_to(root):
            raise ValueError(f'Case outside recording: {case}')
        script = '\n'.join(steps_for(group, machine)) + '\n'
        if (case / 'steps.txt').read_text() != script or sha(case / 'steps.txt') != run['script_sha256']:
            raise ValueError(f'Navigation changed: {name}; recapture this case')
        settings = dict(suite['settings'], orientation=orientation, screenInverted=dark)
        settings.update(group.get('settings', {}))
        configs = {'settings.json': settings, 'state.json': {'showBootScreen': True}, **group.get('config_files', {})}
        for filename, contents in configs.items():
            if json.loads((case / 'config' / filename).read_text()) != contents:
                raise ValueError(f'Settings changed: {name}/{filename}')
        quiet = str(group.get('input_quiet_ms', reference['input_quiet_ms']))
        if run['command'][run['command'].index('--input-quiet') + 1] != quiet:
            raise ValueError(f'Input pacing changed: {name}')
        for filename, digest in run['input_sha256'].items():
            if sha(case / filename) != digest:
                raise ValueError(f'Input changed: {name}/{filename}')
        for screen in run['screens']:
            if sha(root / screen['image']) != screen['png_sha256']:
                raise ValueError(f'Image changed: {name}/{screen["id"]}')
        screens = summarize(case / 'run', group, machine)
        old_ids = [s['id'] for s in run['screens']]
        if sorted(old_ids) != sorted(s['id'] for s in screens):
            raise ValueError(f'Incomplete recorded screen set: {name}')
        for screen in screens:
            if screen['activity_observed'] != screen['activity_expected']:
                raise ValueError(f'Unexpected activity: {name}/{screen["id"]}')
            if machine == 'x3' and len(screen['controller_refreshes']) != screen['panel_refreshes']:
                raise ValueError(f'Incomplete controller trace: {name}/{screen["id"]}')
            screen['image'] = os.path.relpath(case.parent / screen['image'], out)
        run['screens'] = screens
        run['recording_manifest'] = str(root / 'manifest.json')
        runs.append(run)
    meta = {key: reference[key] for key in common}
    meta.update(status='passed', suite_sha256=sha(HERE / 'screens.json'), build=reference['build'], runs=runs,
                harness_sha256={name: sha(HERE / name) for name in ['capture.py', 'fixtures.py', 'fault.py', 'screens.json']},
                recording_sources=recordings, replaced_cases=replaced, extraction_tool_sha256=sha(__file__),
                host_seconds=round(max(r['end_epoch'] for r in recordings) - min(r['start_epoch'] for r in recordings), 3),
                timing_basis='elapsed recording interval including retries; derived from source manifest completion times',
                sum_run_host_seconds=round(sum(r['host_seconds'] for r in runs), 3))
    out.mkdir(parents=True)
    save(out / 'manifest.json', meta)
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', nargs='+', type=Path, help='Later sources replace duplicate case IDs explicitly')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--allow-partial', action='store_true', help='Allow failed source manifests; selected cases must pass')
    args = parser.parse_args()
    meta = assemble([p.resolve() for p in args.sources], args.out.resolve(), args.allow_partial)
    print(f"Validated {len(meta['runs'])} cases and {sum(len(r['screens']) for r in meta['runs'])} screenshots")


if __name__ == '__main__':
    main()
