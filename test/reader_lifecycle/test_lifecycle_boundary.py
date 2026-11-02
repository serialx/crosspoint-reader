"""Pre-atomic ABI: retain original assertions and append current owned boundary checks.

This driver does not remove names or call an EPUB owner with incomplete tables.
Main supplies an already-built current archive and the unchanged original ABI executable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def digest(value):
    return hashlib.sha256(value).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--original-executable', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_LIFECYCLE_ABI_EVIDENCE_DIR'),
                        help='new directory; existing paths are refused')
    args = parser.parse_args()
    root = args.repo.resolve()
    archive = args.archive.resolve()
    original = args.original_executable.resolve()
    temporary = None if args.evidence_dir else tempfile.TemporaryDirectory(prefix='reader-abi-boundary-')
    evidence = args.evidence_dir.resolve() if args.evidence_dir else Path(temporary.name) / 'evidence'
    evidence.mkdir(parents=True, exist_ok=False)
    here = root / 'test/reader_lifecycle'
    draft = Path(__file__).resolve().parent
    boundary = draft / 'epub_boundary_assertions.h'
    acquisition = here / 'test_app_owned_epub.py'
    include = root / 'rust/crates/xp-ffi/include'
    env = dict(os.environ, XP_TEST_RUST_ARCHIVE=str(archive), PYTHONDONTWRITEBYTECODE='1')
    paths = set(include.glob('*.h'))
    paths.update((root / 'rust/crates/xp-ffi/src/app').rglob('*.rs'))
    paths.update(root / name for name in ('rust/Cargo.toml', 'rust/Cargo.lock', 'test/rust_host.py',
                                        'test/ui_runtime/app_leaves.h'))
    paths.update(here / name for name in ('abi.cpp', 'CMakeLists.txt', 'test_app_owned_epub.py',
                                        'app_owned_epub.cpp', 'physical_defaults.h'))
    source_bytes = {path: path.read_bytes() for path in sorted(paths)}
    draft_bytes = {path: path.read_bytes() for path in (Path(__file__).resolve(), boundary)}
    golden_bytes = {path: path.read_bytes() for path in sorted(here.glob('*.golden.gz'))}
    assert len(golden_bytes) == 39, ('expected immutable original golden set', len(golden_bytes))
    binary_hashes = {archive: digest(archive.read_bytes()), original: digest(original.read_bytes())}
    for path, data in source_bytes.items():
        saved = evidence / 'sources/repository' / path.relative_to(root)
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(data)
    for path, data in draft_bytes.items():
        saved = evidence / 'sources/draft' / path.name
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(data)
    for path, data in golden_bytes.items():
        saved = evidence / 'goldens' / path.name
        saved.parent.mkdir(parents=True, exist_ok=True)
        saved.write_bytes(data)
    before = {
        'source_sha256': {str(path): digest(data) for path, data in source_bytes.items()},
        'draft_sha256': {str(path): digest(data) for path, data in draft_bytes.items()},
        'golden_sha256': {str(path): digest(data) for path, data in golden_bytes.items()},
        'binary_sha256': {str(path): value for path, value in binary_hashes.items()},
    }
    (evidence / 'continuity-before.json').write_text(json.dumps(before, indent=2) + '\n')
    commands, results = [], []

    def run(label, argv, timeout=120):
        record = {'label': label, 'argv': [str(value) for value in argv],
                  'environment': {'XP_TEST_RUST_ARCHIVE': str(archive), 'PYTHONDONTWRITEBYTECODE': '1'}}
        commands.append(record)
        (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        try:
            result = subprocess.run(record['argv'], env=env, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as failure:
            (evidence / (label + '.stdout')).write_bytes(failure.stdout or b'')
            (evidence / (label + '.stderr')).write_bytes(failure.stderr or b'')
            record['timeout_seconds'] = timeout
            (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
            raise
        (evidence / (label + '.stdout')).write_bytes(result.stdout)
        (evidence / (label + '.stderr')).write_bytes(result.stderr)
        record['returncode'] = result.returncode
        (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        results.append({'label': label, 'returncode': result.returncode,
                        'stdout_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr)})
        (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        assert result.returncode == 0, (label, result.returncode, result.stderr.decode(errors='replace'))

    try:
        # Keep the entire old assertion matrix, including the still-supported legacy facade cases.
        run('original-reader-lifecycle-abi', [original])
        c = evidence / 'current-headers.c'
        c.write_text('#include <xp_app.h>\n#include <xp_ui_reader.h>\n#include <xp_epub_session.h>\n')
        run('current-c11-boundary', [os.environ.get('CC', 'cc'), '-std=c11', '-Werror',
                                    '-fsyntax-only', '-I', include, c])
        cpp = evidence / 'current-boundary.cpp'
        cpp.write_text('#include "epub_boundary_assertions.h"\n')
        run('current-cxx207-boundary', [os.environ.get('CXX', 'c++'), '-std=c++17', '-UNDEBUG',
                                       '-Werror', '-fsyntax-only', '-I', evidence / 'sources/draft',
                                       '-I', include, cpp])
        # The existing repository driver records its own compile/run logs in this new child directory.
        run('app-owned-acquisition', [sys.executable, acquisition, '--repo', root, '--archive', archive,
                                      '--evidence-dir', evidence / 'app-owned'], timeout=1200)
        child = json.loads((evidence / 'app-owned/result.json').read_text())
        expected = ['compile', 'rust-failure-0', 'rust-failure-1',
                    'native-failure-0', 'native-failure-1', 'native-failure-2', 'native-failure-3',
                    'entry', 'active-pending']
        assert [case['case'] for case in child['cases']] == expected, 'owned acquisition case matrix changed'
        assert all(case['returncode'] == 0 for case in child['cases'])
        assert child['policy_hooks'] == 0
        summary = {
            'declaration': 'reader_lifecycle_abi', 'phase': 'pre-atomic',
            'original_executable': str(original), 'original_assertions_retained': True,
            'current_header_languages': ['C11', 'C++17'], 'physical_leaf_slots': 207,
            'owned_cases': expected[1:], 'policy_hooks': 0,
            'retired_name_removal': False, 'new_epub_null_or_incomplete_table_calls': 0,
            'results': results,
            'scope': 'original ABI matrix + current boundary assertions + existing eight owned acquisition cases',
            'limit': 'no full App render/input/child-result composition claim',
        }
        (evidence / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps(summary))
    finally:
        checks = {}
        for category, values in (('source', source_bytes), ('draft', draft_bytes), ('golden', golden_bytes)):
            checks[category] = {str(path): {'before': digest(data),
                                           'after': digest(path.read_bytes()) if path.exists() else None}
                                for path, data in values.items()}
        checks['binaries'] = {str(path): {'before': value,
                                         'after': digest(path.read_bytes()) if path.exists() else None}
                              for path, value in binary_hashes.items()}
        checks['golden_set_unchanged'] = set(here.glob('*.golden.gz')) == set(golden_bytes)
        (evidence / 'continuity-after.json').write_text(json.dumps(checks, indent=2) + '\n')
        assert checks['golden_set_unchanged'], 'original golden set changed'
        assert all(item['before'] == item['after'] for category in ('source', 'draft', 'golden', 'binaries')
                   for item in checks[category].values()), 'source/archive/executable/golden continuity changed'


if __name__ == '__main__':
    main()
