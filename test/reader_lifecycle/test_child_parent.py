"""Exercise real Rust child ownership against immutable historical traces.

Only the approved ResourceAccess scopes are projected into the original trace.
Every original event stays in order, with exact lock depths outside those scopes.
"""
import argparse
import collections
import gzip
import hashlib
import json
import os
from pathlib import Path
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
    (0, False): ('32c335b25d7ac32a4f6629a0a26745c4bdb8ae34f70305f84a8a2fc497b52f1a', 184674),
    (0, True): ('db7f6c0f41b128acd656f330052df9f4e28e49a81b615ddeee7863790581647e', 182626),
    (1, False): ('5d67216e032f698f4a18d347a9e8ee25daf3c0e556c192191f7f2bc5fcc3ec59', 188190),
    (1, True): ('3a489edc8bdefa78da5c592df6eeb51d16eaa2f157ada14c004aae85b7122a32', 186142),
}
LOCK = b'lock 0 0 lock=1\n'
UNLOCK = b'unlock 0 0 lock=1\n'
PRODUCTION = {
    name: Path('crates/xp-ffi/src/app/epub') / (name + '.rs')
    for name in ('child', 'child_result', 'selection_metadata', 'sync')
}
MUTANTS = (
    ('result-page-bound', 'child',
     'end < unsafe { ReaderSection::page_count(section) }',
     'end <= unsafe { ReaderSection::page_count(section) }'),
    ('sync-one-link', 'sync', 'state.history.depth > 0', 'state.history.depth > 1'),
    ('selection-cancel', 'child_result', '!cancelled && result.kind == app_abi::RESULT_CLIPPING',
     'result.kind == app_abi::RESULT_CLIPPING'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def project_case(lines, strip, counts):
    """Add scopes by their frozen operation/result boundaries, never by candidate data."""
    _, operation, mask, depth = lines[0].split()
    operation, mask = int(operation), int(mask)
    assert depth == b'lock=0' and lines[1].startswith(b'action ')
    action = int(lines[1].split()[1])
    assert 0 <= operation < 5 and 0 <= mask < 512 and 0 <= action < 4
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
            for index in range(begin, end):
                assert index not in changed, ('overlapping projection', name, index)
                line = lines[index]
                if b' lock=' in line:
                    assert line.endswith(b' lock=0\n'), (name, line)
                    changed[index] = line[:-8] + b' lock=1\n'
        counts[name] += 1

    def capture(index, valid):
        if strip and valid:
            assert lines[index:index + 3] == [LOCK, b'strips 0 0 lock=1\n', UNLOCK]
            counts['existing_strip_capture'] += 1
        else:
            scope(index, index, 'selection_capture')

    if operation in (0, 1):
        if operation == 1 or not mask & 8:
            start = 2
            # These are the fixture's initial configuration destructions, before the call.
            if mask & 48 == 48:
                assert lines[start] == b'section-drop 0 0 lock=0\n'
                start += 1
            if mask & 192 == 192:
                assert lines[start] == b'epub-drop 0 0 lock=0\n'
                start += 1
            valid = mask & 48 != 48 and mask & 192 != 192 and not mask & 128
            capture(start, valid)
        result_count = 0
        for index, line in enumerate(lines):
            if line.startswith(b'load '):
                assert line.endswith(b' lock=0\n')
                end = index + 1
                if lines[end].startswith(b'page-new '):
                    end += 1
                scope(index, end, 'page_load')
            elif line.startswith(b'result '):
                result_count += 1
                assert result_count == 1
                assert any(row.startswith(b'new-child selection ') for row in lines[:index])
                if action != 3:
                    end = index + 1
                    if lines[end].startswith(b'offset '):
                        end += 1
                    scope(index + 1, end, 'result_position')
            elif line.startswith(b'paragraph '):
                scope(index, index + 1, 'result_paragraph')
            elif line.startswith(b'add-pos '):
                prefixes = (b'add-pos ', b'add-end ', b'add-words ', b'add-count ',
                            b'add-signature ', b'add-offsets ', b'add-text ')
                assert all(lines[index + offset].startswith(prefix)
                           for offset, prefix in enumerate(prefixes))
                scope(index, index + len(prefixes), 'clipping_insert')
            elif line.startswith(b'lookup-result ') and not mask & 8:
                assert action == 1
                capture(index + 1, True)
    output = []
    for index, line in enumerate(lines):
        output.extend(before[index])
        output.append(changed.get(index, line))
        output.extend(after[index])
    output.extend(before[len(lines)])
    counts['cases'] += 1
    counts['changed_depth_events'] += len(changed)
    return output


def project(raw, strip):
    lines = raw.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if line.startswith(b'case ')]
    assert starts[0] == 0 and len(starts) == 10240
    starts.append(len(lines))
    counts = collections.Counter()
    output = []
    for begin, end in zip(starts, starts[1:]):
        output.extend(project_case(lines[begin:end], strip, counts))
    expected_counts = dict(cases=10240, selection_capture=3092 if not strip else 1920,
                           existing_strip_capture=0 if not strip else 1172, page_load=2662,
                           result_position=270, result_paragraph=90, clipping_insert=90)
    for name, count in expected_counts.items():
        assert counts[name] == count, (name, counts[name], count)
    scopes = sum(counts[name] for name in ('selection_capture', 'page_load', 'result_position',
                                         'result_paragraph', 'clipping_insert'))
    assert len(output) == len(lines) + scopes * 2
    return b''.join(output), dict(counts)


def transformed_fixture(template):
    source = replace_once(template, '// CHILD_RESOURCES', '')
    source = replace_once(source, '// CHILD_METHODS', (HERE / 'child_session_rust.inc').read_text())
    return replace_once(source, '        switch (operation) {',
                        '        ChildSessionBridge childSession(o);\n        switch (operation) {')


def freeze_original(release):
    reader = subprocess.check_output(
        ['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
    methods = '\n'.join(definition(reader, signature) for signature in (
        'void EpubReaderActivity::openDictionaryWordSelect(', 'void EpubReaderActivity::startClipSelection(',
        'void EpubReaderActivity::openFootnoteSelect(', 'void EpubReaderActivity::openClippings(',
        'bool EpubReaderActivity::launchKOReaderSync('))
    fixture = (HERE / 'child_parent_oracle.cpp').read_text().replace('// CHILD_RESOURCES', '').replace('// CHILD_METHODS', methods)
    with tempfile.TemporaryDirectory(prefix='child-original-') as directory:
        work = Path(directory)
        flags = rust_link_flags()
        for strip in (0, 1):
            def run(text):
                (work / 'original.cpp').write_text(text)
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined',
                                f'-DXP_STRIP_RENDER={strip}', *(['-DCHILD_RELEASE_LOG'] if release else []),
                                str(work / 'original.cpp'), *flags, '-o', str(work / 'original')], check=True)
                return subprocess.check_output([str(work / 'original')])
            original = run(fixture)
            name = f'child-parent-{strip}{"-release" if release else ""}.golden.gz'
            (HERE / name).write_bytes(gzip.compress(original, mtime=0))
            for before, after in (
                    ('endPage < section->pageCount', 'endPage <= section->pageCount'),
                    ('if (footnoteDepth > 0)', 'if (footnoteDepth > 1)'),
                    ('if (result.isCancelled) {', 'if (!result.isCancelled) {')):
                wrong = fixture.replace(before, after)
                assert wrong != fixture and run(wrong) != original, (before, strip, release)


def mismatch(actual, expected):
    left, right = actual.splitlines(), expected.splitlines()
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return dict(line=index + 1, actual=[line.decode(errors='replace') for line in left[max(0, index-5):index+6]],
                        expected=[line.decode(errors='replace') for line in right[max(0, index-5):index+6]])
    return dict(actual_lines=len(left), expected_lines=len(right))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--release-log', action='store_true')
    parser.add_argument('--freeze-original', action='store_true')
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_CHILD_EVIDENCE_DIR'))
    args = parser.parse_args()
    if args.freeze_original:
        freeze_original(args.release_log)
        return
    test_name = 'reader_child_parent' + ('_release' if args.release_log else '')
    fixture = transformed_fixture((HERE / 'child_parent_oracle.cpp').read_text())
    source = {name: (ROOT / 'rust' / path).read_bytes() for name, path in PRODUCTION.items()}
    fixture_paths = [HERE / name for name in ('child_parent_oracle.cpp', 'child_session_rust.inc',
                                             'child_session_fixture.h', 'overlay_actions_session_fixture.h',
                                             'test_child_parent.py')]
    fixture_paths.append(ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/child.rs')
    fixture_hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in fixture_paths}
    frozen = {strip: (HERE / f'child-parent-{strip}{"-release" if args.release_log else ""}.golden.gz').read_bytes()
              for strip in (0, 1)}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-child-session-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / test_name if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=True)
            (work / 'child.cpp').write_text(fixture)
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
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', f'-DXP_STRIP_RENDER={strip}',
                                *(['-DCHILD_RELEASE_LOG'] if args.release_log else []),
                                '-I', str(HERE), str(work / 'child.cpp'), *selected_flags, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result

            for strip in (0, 1):
                compressed = frozen[strip]
                expected_sha, expected_lines = GOLDENS[(strip, args.release_log)]
                assert digest(compressed) == expected_sha
                original = gzip.decompress(compressed)
                assert len(original.splitlines()) == expected_lines
                expected, projections = project(original, strip)
                (evidence / f'strip-{strip}.projected').write_bytes(expected)
                baseline = run(f'baseline-{strip}', strip, build(strip) if strip else None)
                assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
                assert baseline.stdout == expected, mismatch(baseline.stdout, expected)
                report = dict(test_name=test_name, strip=strip, cases=10240,
                              original_gzip_sha256=expected_sha, original_trace_sha256=digest(original),
                              original_trace_lines=expected_lines, projected_trace_sha256=digest(expected),
                              projected_trace_lines=len(expected.splitlines()), actual_trace_sha256=digest(baseline.stdout),
                              projection_counts=projections, fixture_sha256=fixture_hashes,
                              candidate_source_sha256={name: digest(data) for name, data in source.items()},
                              resource_contract='per-access Section scopes and full-call clipping insertion; owned Pages and child construction outside new scopes',
                              policy_replacements=['historical bookmark', 'menu', 'applyProgress', 'flush', 'saveProgress',
                                                   'navigateHref', 'clippingAtPoint', 'replace', 'currentPosition',
                                                   'layoutSignature', 'discardOverlay', 'saveLinks', 'push', 'update'],
                              limitations=['historical sync allocation is infallible in this fixture',
                                           'real App child destroy/publish/unlock/result integration remains separate'])
                (evidence / f'strip-{strip}.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(report), flush=True)
                for name, module, before, after in MUTANTS:
                    changed = replace_once(source[module], before.encode(), after.encode())
                    file = copied_rust / PRODUCTION[module]
                    assert file.read_bytes() == source[module]
                    file.write_bytes(changed)
                    try:
                        archive = build(strip)  # A compile failure never counts as rejection.
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
            assert (ROOT / 'rust' / path).read_bytes() == source[name], 'production source changed during child test'
        for strip in (0, 1):
            name = f'child-parent-{strip}{"-release" if args.release_log else ""}.golden.gz'
            assert (HERE / name).read_bytes() == frozen[strip], 'immutable child golden changed'
        for path in fixture_paths:
            assert digest(path.read_bytes()) == fixture_hashes[str(path.relative_to(ROOT))], 'fixture source changed'
    print(f'{test_name}: 10240 cases per strip configuration; exact counted lock projection; three Rust mutants rejected per configuration')


if __name__ == '__main__':
    main()
