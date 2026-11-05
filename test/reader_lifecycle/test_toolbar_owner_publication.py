"""Verify shared publication and actual owner/native toolbar composition."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def once(source, before, after):
    assert source.count(before) == 1, (before, source.count(before))
    return source.replace(before, after, 1)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def exercise(args, evidence):
    root = args.repo.resolve()
    here = root / 'test/reader_lifecycle'
    draft = Path(__file__).resolve().parent
    archive = args.archive.resolve()
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(root / 'test'))
    sys.path.insert(0, str(here))
    from rust_host import rust_link_flags
    import test_toolbar_resource as resource
    env = dict(os.environ, XP_TEST_RUST_ARCHIVE=str(archive), PYTHONDONTWRITEBYTECODE='1')
    paths = {root / path for path in resource.PATHS}
    paths.update(here / name for name in (
        'test_toolbar_publication.py', 'toolbar_publication.cpp', 'test_toolbar_resource.py',
        'toolbar_resource_oracle.cpp', 'test_popup_resource.py',
        'overlay_actions_session_fixture.h', 'overlay_view_session_fixture.h'))
    paths.add(root / 'test/rust_host.py')
    paths.update((root / 'rust/crates/xp-ffi/include').glob('*.h'))
    paths.update((root / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    paths.update((root / 'rust/crates/xp-ui/src/reader').glob('*.rs'))
    paths.update(root / name for name in ('rust/Cargo.toml', 'rust/Cargo.lock',
                                        'rust/crates/xp-ffi/src/app/abi.rs',
                                        'rust/crates/xp-ffi/src/app/result.rs'))
    source_bytes = {path: path.read_bytes() for path in sorted(paths)}
    draft_paths = [Path(__file__).resolve(), draft / 'toolbar_owner_publication.inc']
    draft_bytes = {path: path.read_bytes() for path in draft_paths}
    golden_bytes = {path: path.read_bytes() for path in sorted(here.glob('*.golden.gz'))}
    assert len(golden_bytes) == 39, ('expected immutable original golden set', len(golden_bytes))
    archive_sha = digest(archive.read_bytes())
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
        'archive': str(archive), 'archive_sha256': archive_sha,
    }
    (evidence / 'continuity-before.json').write_text(json.dumps(before, indent=2) + '\n')
    commands, results = [], []

    def execute(label, argv, timeout=120):
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
        return result

    try:
        # Unchanged threaded component, revision-wrap, null-pointer, C ABI and spelling-drift tests.
        shared = execute('shared-component', [sys.executable, here / 'test_toolbar_publication.py'])
        assert shared.returncode == 0, ('shared component failed', shared.returncode)
        sources = {path: source_bytes[root / path].decode() for path in resource.PATHS}
        original_template = source_bytes[here / 'toolbar_resource_oracle.cpp'].decode()
        template = once(original_template, 'int main() {', 'static void resource_contract_main() {')
        template = once(template, 'class GfxRenderer {',
                        'static void (*observeOwnerText)(const XpUiCommand&, const char*) = nullptr;\n'
                        'static void (*beforeOwnerPaintReturn)() = nullptr;\nclass GfxRenderer {')
        template = once(template, '    ++texts;\n',
                        '    ++texts;\n    if (observeOwnerText) { observeOwnerText(command, text); return; }\n')
        template = once(template, '    return paintSucceeds;\n',
                        '    if (beforeOwnerPaintReturn) beforeOwnerPaintReturn();\n    return paintSucceeds;\n')
        template += '\n' + draft_bytes[draft / 'toolbar_owner_publication.inc'].decode()
        flags = rust_link_flags(archive=str(archive))

        def run(label, inputs, expected_marker=None):
            case = evidence / 'cases' / label
            case.mkdir(parents=True, exist_ok=False)
            for path, source in inputs.items():
                (case / path.name).write_text(source)
            cpp = case / 'owner.cpp'
            cpp.write_text(resource.fixture(inputs, template))
            binary = case / 'owner'
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-UNDEBUG', '-O0',
                       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                       '-I', str(case), '-I', str(here), str(cpp), *flags, '-o', str(binary)]
            built = execute(label + '-compile', command)
            assert built.returncode == 0, (label, 'compile failure is not mutant rejection', built.returncode)
            # Baseline runs every original resource assertion; mutants isolate the named owner check.
            ran = execute(label + '-run', [binary, *(['--owner-only'] if expected_marker else [])], timeout=30)
            if expected_marker:
                expected = (expected_marker + '\n').encode()
                assert ran.returncode == 42 and ran.stderr == expected, (
                    label, 'requires clean status42 and exactly the targeted diagnostic', ran.returncode,
                    ran.stdout, ran.stderr)
            else:
                assert ran.returncode == 0 and b'PUBLICATION_FAILURE:' not in ran.stderr, (
                    label, ran.returncode, ran.stdout, ran.stderr)
            results.append({'case': label, 'returncode': ran.returncode,
                            'expected_marker': expected_marker, 'source_sha256': digest(cpp.read_bytes()),
                            'stdout_sha256': digest(ran.stdout), 'stderr_sha256': digest(ran.stderr)})
            (evidence / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        run('owner-native-baseline', sources)
        stale = dict(sources)
        stale[resource.TOOLBAR_SOURCE] = once(stale[resource.TOOLBAR_SOURCE],
            '    xp_ui_reader_toolbar_nav_publish(&nav_, &next);', '    nav_ = next;')
        run('stale-navigation-publication', stale, 'PUBLICATION_FAILURE: stale-navigation-publication')
        invalid = dict(sources)
        invalid[resource.TOOLBAR_SOURCE] = once(invalid[resource.TOOLBAR_SOURCE],
                                              '  ready = valid;', '  ready = true;')
        run('failed-paint-marked-ready', invalid, 'PUBLICATION_FAILURE: failed-paint-marked-ready')
        summary = {'declaration': 'reader_toolbar_publication', 'results': results,
                   'policy_overrides': 0, 'owner_cases': 8,
                   'scope': 'unchanged shared concurrency + actual owner Contents rows through native toolbar/widget',
                   'limits': 'new interleavings are deterministic callbacks, not an additional threaded owner test; App route is not exercised'}
        (evidence / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(json.dumps(summary))
    finally:
        checks = {}
        for category, values in (('source', source_bytes), ('draft', draft_bytes), ('golden', golden_bytes)):
            checks[category] = {str(path): {'before': digest(data),
                                           'after': digest(path.read_bytes()) if path.exists() else None}
                                for path, data in values.items()}
        checks['archive'] = {str(archive): {'before': archive_sha,
                                           'after': digest(archive.read_bytes()) if archive.exists() else None}}
        checks['golden_set_unchanged'] = set(here.glob('*.golden.gz')) == set(golden_bytes)
        (evidence / 'continuity-after.json').write_text(json.dumps(checks, indent=2) + '\n')
        assert checks['golden_set_unchanged'], 'original golden set changed'
        assert all(item['before'] == item['after'] for category in ('source', 'draft', 'golden', 'archive')
                   for item in checks[category].values()), 'source/archive/golden continuity changed'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_TOOLBAR_OWNER_EVIDENCE_DIR'),
                        help='new directory; existing paths are refused')
    args = parser.parse_args()
    if args.evidence_dir:
        evidence = args.evidence_dir.resolve()
        evidence.mkdir(parents=True, exist_ok=False)
        exercise(args, evidence)
    else:
        with tempfile.TemporaryDirectory(prefix='reader-toolbar-owner-publication-') as directory:
            exercise(args, Path(directory))


if __name__ == '__main__':
    main()
