#!/usr/bin/env python3
"""Validate schema-1 recordings and copy portable schema-2 evidence to a new directory."""
import argparse
import copy
import json
import shutil
from pathlib import Path

from capture import HERE, save, sha, summarize
from contracts import declared_cases, executable_identity, expected_contract, validate_recording


def migrate(source, out):
    if out.exists():
        raise ValueError(f'Output already exists: {out}')
    path = source / 'manifest.json'
    original = json.loads(path.read_text())
    if original['schema'] != 1 or original['status'] != 'passed':
        raise ValueError('Migration requires a passed schema-1 recording')
    for name in ['fixtures.py', 'fault.py']:
        if original['harness_sha256'][name] != sha(HERE / name):
            raise ValueError(f'Fixture/fault code changed: {name}')
    if original['xtc_fixture_sha256'] != sha(HERE.parent / 'xtc_diff/generate.py'):
        raise ValueError('XTC fixture code changed')
    suite = json.loads((HERE / 'screens.json').read_text())
    expected = declared_cases(suite)
    meta = copy.deepcopy(original)
    cases = []
    for run in meta['runs']:
        group, machine, orientation, dark = expected[run['name']]
        if (run['machine'], run['orientation'], run['dark'], run['group']) != (machine, orientation, dark, group['id']):
            raise ValueError(f'Case identity mismatch: {run["name"]}')
        if run['exit_code']:
            raise ValueError(f'Failed case: {run["name"]}')
        # Existing image references already survive relocation with their source tree.
        case = (source / run['screens'][0]['image']).resolve().parents[2]
        contract = expected_contract(suite, group, machine, orientation, dark)
        validate_recording(case, run, contract, legacy=True)
        extracted = summarize(case / 'run', group, machine)
        for old, new in zip(run['screens'], extracted):
            if sha(source / old['image']) != old['png_sha256']:
                raise ValueError(f'Image changed: {run["name"]}')
            for key in ['id', 'png_sha256', 'pixels_sha256', 'size', 'activity_expected', 'activity_observed',
                        'refresh_requests', 'controller_refreshes', 'panel_refreshes']:
                if old[key] != new[key]:
                    raise ValueError(f'Historical extraction changed: {run["name"]}/{key}')
            if new['activity_expected'] != new['activity_observed']:
                raise ValueError(f'Unexpected activity: {run["name"]}')
        if len(run['screens']) != len(extracted):
            raise ValueError(f'Incomplete screen set: {run["name"]}')
        run.update(contract=contract, artifact_dir=run['name'], screens=extracted)
        cases.append((case, run['name']))
    # Validate the whole source before creating any new evidence.
    out.mkdir(parents=True)
    shutil.copy2(path, out / 'original-manifest.json')
    for case, name in cases:
        shutil.copytree(case, out / name, ignore=shutil.ignore_patterns('card.img'))
    meta.update(schema=2, executable_identity=executable_identity(original['emulator']),
                suite_sha256=sha(HERE / 'screens.json'),
                harness_sha256={name: sha(HERE / name) for name in ['capture.py', 'contracts.py', 'fixtures.py', 'fault.py', 'screens.json']},
                migration={'source_manifest': str(path), 'source_sha256': sha(path),
                           'original_manifest': 'original-manifest.json', 'tool_sha256': sha(__file__),
                           'validation': 'Regenerated deterministic fixtures, exact config inventory, staging, navigation and pacing; re-extracted startup; preserved existing pixel/refresh checks',
                           'omitted_artifacts': ['card.img (disposable emulator SD image)']})
    save(out / 'manifest.json', meta)
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    meta = migrate(args.source.resolve(), args.out.resolve())
    print(f"Migrated {len(meta['runs'])} validated cases")


if __name__ == '__main__':
    main()
