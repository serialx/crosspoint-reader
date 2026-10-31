"""Recording contracts shared by capture, migration, assembly and selection."""
import json
import tempfile
from pathlib import Path, PurePosixPath

from capture import HERE, records, sha, steps_for
from fixtures import prepare


def executable_identity(identity):
    return {key: identity[key] for key in
            ['cli_sha256', 'cli_version', 'qemu_sha256', 'qemu_version']}


def declared_cases(suite, selection=None):
    selection = selection or {}
    choices = {'machines': ['x3', 'x4'], 'orientations': list(range(4)), 'dark': [0, 1],
               'groups': [g['id'] for g in suite['groups']]}
    for key, values in selection.items():
        if key not in choices or not values or not set(values).issubset(choices[key]):
            raise ValueError(f'Invalid selection: {key}={values}')
        choices[key] = values
    result = {}
    for machine in choices['machines']:
        for orientation in choices['orientations']:
            for dark in choices['dark']:
                for group in suite['groups']:
                    if group['id'] not in choices['groups'] or machine not in group.get('machines', ['x3', 'x4']):
                        continue
                    if orientation and not group.get('rotates'):
                        continue
                    result[f'{machine}-o{orientation}-d{dark}-{group["id"]}'] = (group, machine, orientation, dark)
    if not result:
        raise ValueError('Selection contains no declared cases')
    return result


def input_files(case):
    return {str(p.relative_to(case)): sha(p) for folder in ['config', 'assets']
            for p in sorted((case / folder).rglob('*')) if p.is_file()}


def staged_files(case, puts):
    staged = []
    for source, target in puts:
        source = Path(source)
        paths = sorted(source.rglob('*')) if source.is_dir() else [source]
        for path in paths:
            if path.is_file():
                suffix = path.relative_to(source.parent)
                destination = str(PurePosixPath(target) / suffix.as_posix())
                staged.append({'source': str(path.relative_to(case)), 'destination': destination, 'sha256': sha(path)})
    return staged


def expected_contract(suite, group, machine, orientation, dark):
    settings = dict(suite['settings'], orientation=orientation, screenInverted=dark)
    settings.update(group.get('settings', {}))
    configs = {'settings.json': settings, 'state.json': {'showBootScreen': True}, **group.get('config_files', {})}
    with tempfile.TemporaryDirectory() as tmp:
        case = Path(tmp)
        (case / 'config').mkdir()
        for name, contents in configs.items():
            (case / 'config' / name).write_text(json.dumps(contents, indent=2) + '\n')
        assets = prepare(case, group.get('fixture', 'empty'))
        puts = [(p, '/.crosspoint') for p in sorted((case / 'config').iterdir())] + assets
        return {'fixture': group.get('fixture', 'empty'), 'configs': configs,
                'inputs': input_files(case), 'staged': staged_files(case, puts),
                'steps': steps_for(group, machine), 'input_quiet_ms': group.get('input_quiet_ms', 200),
                'boot': group.get('boot', False), 'fault': group.get('fault'),
                'screens': [{'id': s['id'], 'activity': s['activity']} for s in group['screens']
                            if machine in s.get('machines', ['x3', 'x4'])]}


def validate_recording(case, run, expected, legacy=False):
    if not legacy and run.get('contract') != expected:
        raise ValueError(f'Recording contract changed: {run["name"]}')
    if input_files(case) != expected['inputs'] or run['input_sha256'] != expected['inputs']:
        raise ValueError(f'Fixture/config inventory changed: {run["name"]}')
    script = '\n'.join(expected['steps']) + '\n'
    if (case / 'steps.txt').read_text() != script or sha(case / 'steps.txt') != run['script_sha256']:
        raise ValueError(f'Navigation changed: {run["name"]}')
    cmd = run['command']
    if cmd[cmd.index('--input-quiet') + 1] != str(expected['input_quiet_ms']):
        raise ValueError(f'Input pacing changed: {run["name"]}')
    # Historical absolute paths are provenance; map their case-relative suffixes.
    original_case = Path(cmd[cmd.index('--out') + 1]).parent
    puts = []
    for i, arg in enumerate(cmd):
        if arg == '--put':
            source, target = cmd[i + 1].rsplit('=', 1)
            puts.append((case / Path(source).relative_to(original_case), target))
    if staged_files(case, puts) != expected['staged']:
        raise ValueError(f'Staged destinations changed: {run["name"]}')
    start = records(case / 'run/run.jsonl')[0]
    actual_puts = [(case / Path(p['host']).relative_to(original_case), p['card_dir'])
                   for p in start['card']['staged']]
    if staged_files(case, actual_puts) != expected['staged']:
        raise ValueError(f'Recorded staging changed: {run["name"]}')
    pacing = start['pacing']
    wanted = {'input_quiet_ms': expected['input_quiet_ms'], 'key_hold_ms': 150,
              'panel_idle_ms': 0 if expected['boot'] else 300, 'settle_ms': 0 if expected['boot'] else 200}
    if pacing != wanted:
        raise ValueError(f'Recorded pacing changed: {run["name"]}')


def case_directory(root, run):
    path = Path(run['artifact_dir'])
    if path.is_absolute():
        raise ValueError('Case artifact directory must be relative to manifest')
    return (root / path).resolve()
