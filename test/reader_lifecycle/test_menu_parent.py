"""Exercise Rust menu parents, retained child results, and More popup callbacks.

The immutable original traces receive only explicitly counted resource scopes.
The candidate trace is compared whole, without filtering callbacks or probes.
"""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

GOLDENS = {
    0: ('47b0685dc792a957d69975eb22b4de3321e140dd225fa8eb6ed0e059e6ee31f7', 446640),
    1: ('e2f05caa521d052b631c780fb38f2f1d77eb35f61f12a8ae32ad78f06b588bcf', 472944),
}
LOCK = b'lock 1 0\n'
UNLOCK = b'unlock 1 0\n'
PRODUCTION = {name: Path('crates/xp-ffi/src/app/epub') / (name + '.rs')
              for name in ('menu', 'menu_more', 'child_result', 'overlay_actions')}
MUTANTS = (
    ('frontlight-save-omitted', 'menu_more', 'unsafe { (self.runtime.settings_save)(&settings) };',
     'let _ = &settings;'),
    ('cancelled-menu-confirmed', 'child_result', 'if !cancelled {', 'if true {'),
    ('text-not-filtered', 'overlay_actions', 'action != Action::Chapter as u8 && action != Action::Text as u8',
     'action != Action::Chapter as u8'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def generated_i18n_directory(archive):
    headers = sorted((Path(archive).resolve().parent / 'build').glob('xp-core-*/out/I18nKeys.h'))
    assert headers, ('missing generated I18nKeys.h', str(archive))
    assert len({path.read_bytes() for path in headers}) == 1, 'conflicting generated I18nKeys.h'
    return headers[0].parent


def project_case(lines, strip, counts):
    _, operation, action = lines[0].split()
    operation, action = int(operation), int(action)
    _, mask, cancelled = lines[1].split()
    mask, cancelled = int(mask), int(cancelled)
    assert 0 <= operation < 4 and -1 <= action < 20 and 0 <= mask < 128 and cancelled in (0, 1)
    before = collections.defaultdict(list)
    after = collections.defaultdict(list)
    changed = {}

    def scope(begin, end, name):
        assert 0 <= begin <= end <= len(lines)
        before[begin].append(LOCK)
        if begin == end:
            before[begin].append(UNLOCK)
        else:
            after[end - 1].append(UNLOCK)
        counts[name] += 1

    start = 2
    if mask & 64:
        assert lines[start] == b'new-toolbar 0 0\n'
        counts['initial_toolbar_unchanged'] += 1
        start += 1
    if operation in (1, 2) and action == 14:
        qr = start
        if operation == 2:
            assert lines[qr:qr + 3] == [LOCK, b'settle 1 0\n', UNLOCK]
            qr += 3
            assert lines[qr] == f'discard {int(bool(mask & 32))} 0\n'.encode()
            qr += 1
        if strip:
            assert lines[qr:qr + 3] == [LOCK, b'release-strips 1 0\n', UNLOCK]
            qr += 3
        end = qr
        if mask & 1 and not mask & 8:
            assert lines[qr] == b'page-text 0 0\n'
            changed[qr] = b'page-text 1 0\n'
            counts['page_text_depth'] += 1
            end += 1
        scope(qr, end, 'qr_capture')
    for index, line in enumerate(lines):
        if line == b'new-toolbar 0 0\n' and index >= start:
            scope(index + 1, index + 1, 'toolbar_install')
        elif line.startswith(b'discard '):
            assert line in (b'discard 0 0\n', b'discard 1 0\n')
            # The first argument is the stored flag, not a lock-depth observation.
            scope(index, index + 1, 'discard')
        elif line == b'load-bookmarks 0 0\n':
            scope(index, index + 1, 'bookmark_reload')
        elif line.startswith(b'popup-ids '):
            assert lines[index + 1:index + 5] == [f'id {value} 0\n'.encode() for value in range(4)]
            scope(index, index + 5, 'popup_ids')
        elif line.startswith(b'popup-labels '):
            assert lines[index + 1:index + 6] == [b'label [Off]\n', b'label [1]\n', b'label [3]\n',
                                                b'label [6]\n', b'label [12]\n']
            scope(index, index + 6, 'popup_labels')
        elif line.startswith(b'build-clips '):
            assert operation == 3
            scope(index + 1, index + 1, 'more_publication')
    output = []
    for index, line in enumerate(lines):
        output.extend(before[index])
        output.append(changed.get(index, line))
        output.extend(after[index])
    output.extend(before[len(lines)])
    counts['cases'] += 1
    return output


def project(raw, strip):
    lines = raw.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if line.startswith(b'case ')]
    assert starts[0] == 0 and len(starts) == 11264
    starts.append(len(lines))
    counts = collections.Counter()
    output = []
    for begin, end in zip(starts, starts[1:]):
        output.extend(project_case(lines[begin:end], strip, counts))
    expected = dict(cases=11264, initial_toolbar_unchanged=5632, toolbar_install=400, discard=4704,
                    bookmark_reload=512, popup_ids=256, popup_labels=256, qr_capture=512,
                    page_text_depth=128, more_publication=256)
    assert dict(counts) == expected, (dict(counts), expected)
    added = sum(counts[name] for name in ('toolbar_install', 'discard', 'bookmark_reload', 'popup_ids',
                                        'popup_labels', 'qr_capture', 'more_publication'))
    assert added == 6896 and len(output) == len(lines) + added * 2
    return b''.join(output), dict(counts)


def transformed_fixture(template):
    # Give the frozen fixture's tiny enum a distinct name from the generated firmware IDs.
    source = re.sub(r'\bStrId\b', 'FixtureStrId', template)
    source = replace_once(source, '// MENU_METHODS', (HERE / 'menu_session_rust.inc').read_text())
    return replace_once(source, '          switch (operation) {',
                        '          MenuSessionBridge menuSession(a);\n          switch (operation) {')


def freeze_original(release):
    source = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'],
                                     cwd=ROOT, text=True)
    methods = '\n'.join(definition(source, signature) for signature in (
        'void EpubReaderActivity::openReaderMenu(', 'void EpubReaderActivity::onReaderMenuConfirm(',
        'void EpubReaderActivity::buildMoreActions(', 'void EpubReaderActivity::activateMoreRow('))
    fixture = (HERE / 'menu_parent_oracle.cpp').read_text().replace('// MENU_METHODS', methods)
    with tempfile.TemporaryDirectory(prefix='menu-original-') as directory:
        work = Path(directory)
        flags = rust_link_flags()
        for strip in (0, 1):
            def run(text):
                (work / 'original.cpp').write_text(text)
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-Wno-switch', '-fsanitize=address,undefined',
                                f'-DXP_STRIP_RENDER={strip}', *(['-DMENU_RELEASE_LOG'] if release else []),
                                str(work / 'original.cpp'), *flags, '-o', str(work / 'original')], check=True)
                return subprocess.check_output([str(work / 'original')])
            original = run(fixture)
            name = f'menu-parent-{strip}{"-release" if release else ""}.golden.gz'
            (HERE / name).write_bytes(gzip.compress(original, mtime=0))
            for before, after in (('return saveOk;', 'return true;'), ('return toolbar;', 'return false;'),
                                  ('event("clear-deferred", lockDepth);', 'event("clear-deferred", 0);')):
                wrong = fixture.replace(before, after)
                assert wrong != fixture and run(wrong) != original


def mismatch(actual, expected):
    left, right = actual.splitlines(), expected.splitlines()
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return dict(line=index + 1,
                        actual=[line.decode(errors='replace') for line in left[max(0, index - 5):index + 6]],
                        expected=[line.decode(errors='replace') for line in right[max(0, index - 5):index + 6]])
    return dict(actual_lines=len(left), expected_lines=len(right))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-log', action='store_true')
    parser.add_argument('--freeze-original', action='store_true')
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_MENU_EVIDENCE_DIR'))
    args = parser.parse_args()
    if args.freeze_original:
        freeze_original(args.release_log)
        return
    test_name = 'reader_menu_parent' + ('_release' if args.release_log else '')
    fixture = transformed_fixture((HERE / 'menu_parent_oracle.cpp').read_text())
    source = {name: (ROOT / 'rust' / path).read_bytes() for name, path in PRODUCTION.items()}
    fixture_paths = [HERE / name for name in ('menu_parent_oracle.cpp', 'menu_session_rust.inc',
                                             'menu_session_fixture.h', 'overlay_actions_session_fixture.h',
                                             'test_menu_parent.py')]
    fixture_paths.append(ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/menu.rs')
    fixture_hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in fixture_paths}
    frozen = {strip: (HERE / f'menu-parent-{strip}{"-release" if args.release_log else ""}.golden.gz').read_bytes()
              for strip in (0, 1)}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-menu-session-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / test_name if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=True)
            (work / 'menu.cpp').write_text(fixture)
            flags = rust_link_flags()
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            target = work / 'target'

            def build(strip):
                features = 'app-test,reader-strips' if strip else 'app-test'
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', features], cwd=copied_rust, check=True)
                return target / 'release/libxp_ffi.a'

            def run(name, strip, archive=None):
                binary = work / name
                selected_flags = flags if archive is None else rust_link_flags(archive=str(archive))
                selected_archive = flags[-1] if archive is None else archive
                i18n = generated_i18n_directory(selected_archive)
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-Wno-switch', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', f'-DXP_STRIP_RENDER={strip}',
                                *(['-DMENU_RELEASE_LOG'] if args.release_log else []),
                                '-I', str(HERE), '-I', str(i18n), str(work / 'menu.cpp'),
                                *selected_flags, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result

            for strip in (0, 1):
                compressed = frozen[strip]
                expected_sha, expected_lines = GOLDENS[strip]
                assert digest(compressed) == expected_sha
                original = gzip.decompress(compressed)
                assert len(original.splitlines()) == expected_lines
                expected, projections = project(original, strip)
                (evidence / f'strip-{strip}.projected').write_bytes(expected)
                baseline = run(f'baseline-{strip}', strip, build(strip) if strip else None)
                assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
                assert baseline.stdout == expected, mismatch(baseline.stdout, expected)
                report = dict(test_name=test_name, strip=strip, cases=11264,
                              original_gzip_sha256=expected_sha, original_trace_sha256=digest(original),
                              original_trace_lines=expected_lines, projected_trace_sha256=digest(expected),
                              projected_trace_lines=len(expected.splitlines()), actual_trace_sha256=digest(baseline.stdout),
                              projection_counts=projections, fixture_sha256=fixture_hashes,
                              candidate_source_sha256={name: digest(data) for name, data in source.items()},
                              resource_contract='toolbar/More publication, backup disposal, popup replacement, QR capture and bookmark reload',
                              policy_replacements=['19 named historical MenuHooks', 'toolbar-menu predicate', 'currentToc',
                                                   'discard/settle', 'loadBookmarks', 'render/push overlay', 'update/push child'],
                              limitations=['historical child constructors are infallible in this fixture',
                                           'real App child destroy/publish/unlock/result integration remains separate'])
                (evidence / f'strip-{strip}.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(report), flush=True)
                for name, module, before, after in MUTANTS:
                    changed = replace_once(source[module], before.encode(), after.encode())
                    file = copied_rust / PRODUCTION[module]
                    assert file.read_bytes() == source[module]
                    file.write_bytes(changed)
                    try:
                        archive = build(strip)  # Compilation failure is not a killed mutant.
                        rejected = run(f'{name}-{strip}', strip, archive)
                        assert rejected.returncode != 0 or rejected.stdout != expected, ('actual Rust mutant survived', name, strip)
                        print(json.dumps(dict(test_name=test_name, strip=strip, mutant=name, kind='production-rust',
                                              source=str(PRODUCTION[module]), replacement_count=1,
                                              candidate_source_sha256=digest(source[module]), mutant_source_sha256=digest(changed),
                                              returncode=rejected.returncode, stdout_sha256=digest(rejected.stdout),
                                              stderr_sha256=digest(rejected.stderr))), flush=True)
                    finally:
                        file.write_bytes(source[module])
    finally:
        for name, path in PRODUCTION.items():
            assert (ROOT / 'rust' / path).read_bytes() == source[name], 'production source changed during menu test'
        for strip in (0, 1):
            name = f'menu-parent-{strip}{"-release" if args.release_log else ""}.golden.gz'
            assert (HERE / name).read_bytes() == frozen[strip], 'immutable menu golden changed'
        for path in fixture_paths:
            assert digest(path.read_bytes()) == fixture_hashes[str(path.relative_to(ROOT))], 'fixture source changed'
    print(f'{test_name}: 11264 cases per strip configuration; exact counted lock projection; three Rust mutants rejected per configuration')


if __name__ == '__main__':
    main()
