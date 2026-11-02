"""Compare real EPUB overlay input and retained results with the frozen parent trace.

Only the 74 approved resource scopes are added to the expected trace. Candidate
output, including lazy probes and native destructors, is compared without filtering.
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

GOLDEN = 'overlay-input.golden.gz'
GZIP_SHA = '4c5782110b74a8300186c9b82e7e1edd55ae0728ab9fbd4b75ad0f87f1c1b652'
RAW_SHA = '5556394dce73c7162e1ee81150872ce71cccd996813e516ba0bdee46fff88beb'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/overlay_input.rs')
EVENT = re.compile(rb'(\d+) (\S+) (-?\d+) (-?\d+) lock=(\d+)\n')
HEADER = re.compile(rb'case=(\d+) mode=(\d+) scenario=(\d+) variant=(\d+)\n')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def replace_regex_once(source, pattern, after):
    changed, count = re.subn(pattern, after, source)
    assert count == 1, ('expected one replacement', pattern, count)
    return changed


def project(raw):
    lines = raw.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if line.startswith(b'case=')]
    assert len(starts) == 1728 and starts[0] == 0 and len(lines) == 24067
    starts.append(len(lines))
    counts = collections.Counter()
    output = []
    for case, (begin, end) in enumerate(zip(starts, starts[1:])):
        block = lines[begin:end]
        header = HEADER.fullmatch(block[0])
        assert header and int(header[1]) == case
        mode, scenario, variant = (int(header[index]) for index in (2, 3, 4))
        assert mode == case // 432 + 1 and scenario == case % 432 // 12 and variant == case % 12
        events = [(index, EVENT.fullmatch(line)) for index, line in enumerate(block[1:], 1)
                  if not line.startswith(b'state ')]
        assert all(match for _, match in events)
        names = [match[2] for _, match in events]
        replacements = {}
        toc_live = False
        depth = 0
        for ordinal, (index, match) in enumerate(events):
            assert int(match[1]) == ordinal
            name, a, b, observed_depth = match[2], int(match[3]), int(match[4]), int(match[5])
            if name == b'lock':
                depth += 1
                assert depth == 1
            assert observed_depth == depth
            if name == b'unlock':
                depth -= 1
            if name == b'toc-create':
                assert not toc_live
                toc_live = True
            elif name == b'toc-drop':
                assert toc_live and names[ordinal - 1] == b'update'
                toc_live = False
            elif name == b'discard':
                assert (a, b, observed_depth) == (0, 0, 0)
                if mode == 2:
                    assert toc_live and names[ordinal - 1] in (b'unlock', b'toc-create')
                    assert names[ordinal + 1:ordinal + 3] == [b'update', b'toc-drop']
                    counts['toc_discard'] += 1
                else:
                    assert mode == 3 and not toc_live and names[ordinal - 1] == b'dismiss'
                    assert names[ordinal + 1:ordinal + 8] == [b'lock', b'settle', b'unlock', b'registry',
                                                           b'font-picker-create', b'start-child', b'font-picker-drop']
                    counts['picker_discard'] += 1
                if mode == 3:
                    dismiss_index, dismiss = events[ordinal - 1]
                    assert (int(dismiss[3]), int(dismiss[4]), int(dismiss[5])) == (0, 0, 0)
                    replacements[dismiss_index] = [(b'lock', 0, 0, 1), (b'dismiss', 0, 0, 1)]
                    replacements[index] = [(b'discard', a, b, 1), (b'unlock', 0, 0, 1)]
                else:
                    replacements[index] = [(b'lock', 0, 0, 1), (b'discard', a, b, 1), (b'unlock', 0, 0, 1)]
            if name in (b'width', b'height', b'toc-create', b'toc-drop', b'font-picker-create', b'child-result'):
                counts[name.decode()] += 1
        assert depth == 0 and not toc_live
        output.append(block[0])
        ordinal = 0
        for index, line in enumerate(block[1:], 1):
            match = EVENT.fullmatch(line)
            if match is None:
                assert line.startswith(b'state ')
                output.append(line)
                continue
            values = replacements.get(index, [(match[2], int(match[3]), int(match[4]), int(match[5]))])
            for name, a, b, depth in values:
                output.append(f'{ordinal} {name.decode()} {a} {b} lock={depth}\n'.encode())
                ordinal += 1
        counts['cases'] += 1
    expected = {'cases': 1728, 'toc_discard': 55, 'picker_discard': 19, 'width': 72, 'height': 36,
                'toc-create': 55, 'toc-drop': 55, 'font-picker-create': 19, 'child-result': 17}
    assert dict(counts) == expected, (dict(counts), expected)
    assert len(output) == len(lines) + 148
    return b''.join(output), dict(counts)


def fixture_support(fixture, input_header, nav):
    swipe = re.search(r'enum class SwipeDir \{[^}]+\};', input_header)[0]
    fixture = replace_once(fixture, '// SWIPE_ENUM', swipe)
    methods = '\n'.join(definition(nav, 'int ButtonNavigator::' + name)
                        for name in ('nextIndex(', 'previousIndex('))
    return replace_once(fixture, '// NAV_METHODS', methods)


def transformed_fixture():
    fixture = (HERE / 'overlay_oracle.cpp').read_text()
    fixture = replace_once(fixture, '// OVERLAY_METHODS', (HERE / 'overlay_input_session_rust.inc').read_text())
    fixture = replace_once(fixture, '        r.handleOverlayInput();\n        r.dump();',
                          '        OverlayInputSessionBridge overlaySession(r);\n'
                          '        r.handleOverlayInput();\n        r.dump();')
    return fixture_support(fixture, (ROOT / 'src/MappedInputManager.h').read_text(),
                           (ROOT / 'src/util/ButtonNavigator.cpp').read_text())


def freeze_original():
    source = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'],
                                     cwd=ROOT, text=True)
    input_header = subprocess.check_output(['git', 'show', '8d534a99:src/MappedInputManager.h'], cwd=ROOT, text=True)
    fixture = replace_once((HERE / 'overlay_oracle.cpp').read_text(), '// OVERLAY_METHODS',
                           definition(source, 'void EpubReaderActivity::handleOverlayInput()'))
    fixture = fixture_support(fixture, input_header, (ROOT / 'src/util/ButtonNavigator.cpp').read_text())
    with tempfile.TemporaryDirectory(prefix='overlay-input-original-') as directory:
        work = Path(directory)
        (work / 'original.cpp').write_text(fixture)
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', str(work / 'original.cpp'), '-o', str(work / 'original')],
                       check=True)
        actual = subprocess.check_output([str(work / 'original')])
        (HERE / GOLDEN).write_bytes(gzip.compress(actual, mtime=0))


def rust_mutants(original):
    # This scope contains exactly the synchronous TOC sink and its outer fallback.
    begin = original.index('    fn activate_contents_row(')
    end = original.index('    fn activate_overlay_row(', begin)
    scope = original[begin:end]
    moved = replace_once(scope, 'owner.request_update();', '')
    moved = replace_regex_once(moved, r'if\s+!capture\.delivered\s*\{\s*self\.request_update\(\);\s*\}',
                              'self.request_update();')
    yield 'toc-update-after-drop', original[:begin] + moved + original[end:]
    yield 'picker-origin-swapped', replace_once(original, 'self.open_text_settings(true);',
                                                'self.open_text_settings(false);')
    yield 'lazy-press-order-swapped', replace_regex_once(
        original,
        r'self\.mapped_button\(UP, ButtonQuery::Press, 0\)\s*\|\|\s*'
        r'self\.mapped_button\(LEFT, ButtonQuery::Press, 0\)',
        'self.mapped_button(LEFT, ButtonQuery::Press, 0) || self.mapped_button(UP, ButtonQuery::Press, 0)')


def mismatch(actual, expected):
    left, right = actual.splitlines(), expected.splitlines()
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return {'line': index + 1,
                    'actual': [line.decode(errors='replace') for line in left[max(0, index - 4):index + 5]],
                    'expected': [line.decode(errors='replace') for line in right[max(0, index - 4):index + 5]]}
    return {'actual_lines': len(left), 'expected_lines': len(right)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--freeze-original', action='store_true')
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_OVERLAY_INPUT_EVIDENCE_DIR'))
    args = parser.parse_args()
    if args.freeze_original:
        freeze_original()
        return
    golden = (HERE / GOLDEN).read_bytes()
    assert digest(golden) == GZIP_SHA
    raw = gzip.decompress(golden)
    assert digest(raw) == RAW_SHA
    expected, counts = project(raw)
    fixture = transformed_fixture()
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_text()
    paths = sorted((ROOT / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    paths.extend(HERE / name for name in ('overlay_oracle.cpp', 'overlay_input_session_rust.inc',
                                         'overlay_input_session_fixture.h', 'overlay_actions_session_fixture.h',
                                         'test_overlay_input.py'))
    paths.extend(ROOT / path for path in ('src/MappedInputManager.h', 'src/util/ButtonNavigator.cpp',
                                          'rust/crates/xp-ffi/include/xp_epub_session.h',
                                          'rust/crates/xp-ui/src/reader/input.rs',
                                          'rust/crates/xp-ui/src/reader/epub_overlay.rs',
                                          'rust/crates/xp-ui/src/model.rs'))
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in paths}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-overlay-input-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / 'reader_overlay_input' if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=True)
            (evidence / 'projected.trace').write_bytes(expected)
            (evidence / 'projection.json').write_text(json.dumps(counts, indent=2) + '\n')
            (work / 'overlay.cpp').write_text(fixture)
            flags = rust_link_flags()

            def run(name, archive=None):
                binary = work / name
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(HERE), str(work / 'overlay.cpp'), *link, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result

            baseline = run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == expected, mismatch(baseline.stdout, expected)
            summary = {'test_name': 'reader_overlay_input', 'cases': 1728, 'original_gzip_sha256': GZIP_SHA,
                       'original_trace_sha256': RAW_SHA, 'original_trace_lines': 24067,
                       'projected_trace_sha256': digest(expected), 'projected_trace_lines': len(expected.splitlines()),
                       'actual_trace_sha256': digest(baseline.stdout), 'projection_counts': counts,
                       'source_sha256': hashes,
                       'policy': 'ordinary owner input/result methods; historical named stubs; no output filtering'}
            (evidence / 'baseline.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            target = work / 'target'
            for name, changed in rust_mutants(production):
                (copied_rust / PRODUCTION).write_text(changed)
                # A build error is a test error, never a killed mutant.
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result = run(name, target / 'release/libxp_ffi.a')
                killed = result.returncode != 0 or result.stdout != expected
                record = {'test_name': 'reader_overlay_input', 'mutant': name, 'kind': 'production-rust',
                          'source': str(PRODUCTION), 'original_source_sha256': digest(production.encode()),
                          'mutant_source_sha256': digest(changed.encode()), 'returncode': result.returncode,
                          'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr), 'killed': killed}
                (evidence / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
                print(json.dumps(record), flush=True)
                assert killed, ('actual Rust mutant survived', name)
    finally:
        assert (HERE / GOLDEN).read_bytes() == golden, 'immutable golden changed'
        for path, before in hashes.items():
            assert digest((ROOT / path).read_bytes()) == before, ('source changed during test', path)


if __name__ == '__main__':
    main()
