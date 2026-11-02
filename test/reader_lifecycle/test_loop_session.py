"""Compare two ordinary Rust reader-loop steps with the immutable native trace.

Only the 144 approved discard scopes are added to the golden expectation.
Every candidate event and both observations remain in the byte comparison.
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
    False: ('loop.golden.gz', '2c942dbe59aac7f1248497a01d49e7f1928ea4344498125129ac0a4518dc2a4d',
            'ef3f8a82847d37f29b2ad005ef71b4faff2eb026d662e367b666bdfc7604cf22', 316808),
    True: ('loop-release.golden.gz', '9a7fdd7ad2545507745a31020f4e34ea2ef3737a252171bb4de3bddd35a56992',
           '38f19b63c6c0390e5e280f1cb63d739e40444d7bc0e1f8391887242e99800e0b', 309908),
}
EPUB = Path('crates/xp-ffi/src/app/epub')
EVENT = re.compile(rb'(\d+) (\S+) (-?\d+) (-?\d+)\n')
HEADER = re.compile(rb'case (\d+) (\d+) (\d+)\n')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def project(raw, release):
    lines = raw.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if line.startswith(b'case ')]
    assert starts[0] == 0 and len(starts) == 4608 and len(lines) == GOLDENS[release][3]
    starts.append(len(lines))
    counts = collections.Counter()
    output = []
    for case, (begin, end) in enumerate(zip(starts, starts[1:])):
        block = lines[begin:end]
        header = HEADER.fullmatch(block[0])
        assert header and tuple(int(header[index]) for index in (1, 2, 3)) == (case // 144, case % 144 // 4, case % 4)
        phase = int(header[1])
        output.append(block[0])
        original_index = projected_index = states = depth = attempts = 0
        for index, line in enumerate(block[1:], 1):
            if line.startswith(b'state '):
                assert depth == attempts == 0
                states += 1
                output.append(line)
                continue
            match = EVENT.fullmatch(line)
            assert match and int(match[1]) == original_index, line
            original_index += 1
            name, a, b = match[2], int(match[3]), int(match[4])
            if name == b'lock':
                assert depth == attempts == 0 and (a, b) == (0, 0)
                depth = attempts = 1
            elif name == b'try':
                assert depth == attempts == 0 and a in (0, 1) and b == 0
                attempts = 1; depth = a
            elif name == b'unlock':
                assert attempts == 1 and depth == a and b == 0
                attempts = depth = 0
            if name == b'overlay-discard':
                assert phase == 17 and states == 0 and depth == attempts == 0 and (a, b) == (0, 0)
                following = EVENT.fullmatch(block[index + 1])
                assert following and following.groups()[1:] == (b'update', b'0', b'0')
                values = [(b'lock', 0, 0), (name, a, b), (b'unlock', 1, 0)]
                counts['discard_scopes'] += 1
            else:
                values = [(name, a, b)]
            for event, first, second in values:
                output.append(f'{projected_index} {event.decode()} {first} {second}\n'.encode())
                projected_index += 1
            counts[name.decode()] += 1
        assert states == 2 and depth == attempts == 0
        counts['cases'] += 1
    assert counts['discard_scopes'] == counts['overlay-discard'] == 144
    assert counts['cases'] == 4608
    assert counts['heap-free'] == 7102 and counts['heap-max'] == 6820
    assert counts['framebuffer'] == 7524 and counts['font-cache'] == 3309
    assert counts['read-folder'] == 282 and counts['build-start'] == 426
    assert len(output) == len(lines) + 288
    return b''.join(output), dict(counts)


def transformed_fixture(template):
    source = replace_once(template, '#include "loop_methods.inc"', (HERE / 'loop_session_rust.inc').read_text())
    original_state = definition(source, 'static void state(')
    source = replace_once(source, original_state, '''static void state(const EpubReaderActivity& owner) {
  assert(&loop_session::bridge().owner == &owner);
  loop_session::bridge().printState();
}''')
    marker = '        std::printf("case %u %u %u\\n", phase, action, edge);\n        r.loop();'
    return replace_once(source, marker, '        std::printf("case %u %u %u\\n", phase, action, edge);\n'
                                        '        loop_session::Bridge loopSession(r);\n        r.loop();')


def historical_fixture(template):
    source = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'],
                                     cwd=ROOT, text=True)
    utils = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/ReaderUtils.h'], cwd=ROOT, text=True)
    constants = re.findall(r'constexpr unsigned long (?:GO_HOME_MS|GO_BACK_OR_HOME_MS|BOOKMARK_MESSAGE_DURATION_MS) = [^;]+;', utils)
    assert len(constants) == 3
    methods = 'namespace ReaderUtils {\n' + '\n'.join(constants) + '\n}\n'
    methods += definition(source, 'void EpubReaderActivity::loop()')
    return replace_once(template, '#include "loop_methods.inc"', methods)


def rust_mutants(owner, maintenance):
    button_probe = '        let buttons = self.button_turn();\n'
    pending = '        if self.model(|state| state.manual_turn != 0) && !guard {\n'
    changed = replace_once(owner, button_probe, '')
    yield 'buttons-before-pending-turn', EPUB / 'loop_owner.rs', replace_once(changed, pending, button_probe + pending)
    changed = replace_once(owner, 'self.page_turn(!previous)', 'self.page_turn(next)')
    changed = replace_once(changed, 'self.note_page_turn(!previous, succeeded)', 'self.note_page_turn(next, succeeded)')
    yield 'next-overrides-previous', EPUB / 'loop_owner.rs', changed
    begin = maintenance.index('    pub(super) fn loop_prewarm(')
    end = maintenance.index('    pub(super) fn loop_partial_build(', begin)
    scope = maintenance[begin:end]
    gate = '        if unsafe { (self.leaves.log_enabled)(0) } != 0 {\n'
    changed = replace_once(scope, gate, '        unsafe { (self.runtime.now_ms)() };\n' + gate)
    yield 'forced-prewarm-end-clock', EPUB / 'loop_maintenance.rs', maintenance[:begin] + changed + maintenance[end:]


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
    parser.add_argument('--release', action='store_true')
    parser.add_argument('--original-executable', type=Path)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_LOOP_EVIDENCE_DIR'))
    args = parser.parse_args()
    golden_name, gzip_sha, raw_sha, raw_lines = GOLDENS[args.release]
    golden = (HERE / golden_name).read_bytes()
    assert digest(golden) == gzip_sha
    raw = gzip.decompress(golden)
    assert digest(raw) == raw_sha
    expected, counts = project(raw, args.release)
    template = (HERE / 'loop_oracle.cpp').read_text()
    fixture = transformed_fixture(template)
    paths = [HERE / name for name in ('loop_oracle.cpp', 'loop_session_fixture.h', 'loop_session_rust.inc',
                                     'overlay_actions_session_fixture.h', 'test_loop_session.py')]
    paths.extend(sorted((ROOT / 'rust' / EPUB).rglob('*.rs')))
    paths.extend(ROOT / name for name in ('rust/crates/xp-ffi/include/xp_epub_session.h',
                                         'rust/crates/xp-ffi/include/xp_ui_reader.h',
                                         'rust/crates/xp-ui/src/reader/render.rs',
                                         'rust/crates/xp-ui/src/reader/input.rs'))
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in paths}
    sources = {EPUB / name: (ROOT / 'rust' / EPUB / name).read_text()
               for name in ('loop_owner.rs', 'loop_maintenance.rs')}
    test_name = 'reader_loop_oracle' + ('_release' if args.release else '')
    try:
        with tempfile.TemporaryDirectory(prefix='reader-loop-session-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / test_name if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=True)
            (evidence / 'projected.trace').write_bytes(expected)
            (evidence / 'projection.json').write_text(json.dumps(counts, indent=2) + '\n')
            flags = rust_link_flags()

            def compile_fixture(name, code, archive=None):
                cpp = work / (name + '.cpp'); cpp.write_text(code)
                binary = work / name
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                f'-DLOOP_LOG_LEVEL={1 if args.release else 2}', '-I', str(HERE), '-I', str(ROOT / 'src'),
                                str(cpp), *link, '-o', str(binary)], check=True)
                return binary

            def capture(name, binary):
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result

            native = args.original_executable or compile_fixture('original', historical_fixture(template))
            original = capture('original', native)
            assert original.returncode == 0, original.stderr.decode(errors='replace')
            assert original.stdout == raw, mismatch(original.stdout, raw)
            candidate = capture('candidate', compile_fixture('candidate', fixture))
            assert candidate.returncode == 0, candidate.stderr.decode(errors='replace')
            assert candidate.stdout == expected, mismatch(candidate.stdout, expected)
            summary = {'test_name': test_name, 'cases': 4608, 'steps': 9216, 'release_log': args.release,
                       'original_gzip_sha256': gzip_sha, 'original_trace_sha256': raw_sha, 'original_trace_lines': raw_lines,
                       'projected_trace_sha256': digest(expected), 'projected_trace_lines': len(expected.splitlines()),
                       'candidate_trace_sha256': digest(candidate.stdout), 'projection_counts': counts,
                       'source_sha256': hashes, 'policy': 'one Rust owner per case; two real loop steps; no candidate filtering',
                       'publication': 'exact five-fact sequence at create and after each build attempt; complete=0, estimate=pageCount',
                       'native_baseline': str(native)}
            (evidence / 'baseline.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
            copied_repo = work / 'source'; copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            target = work / 'target'
            for name, path, changed in rust_mutants(sources[EPUB / 'loop_owner.rs'], sources[EPUB / 'loop_maintenance.rs']):
                for source_path, original_source in sources.items():
                    (copied_rust / source_path).write_text(original_source)
                (copied_rust / path).write_text(changed)
                # Only a successful compile followed by divergence counts as a killed mutant.
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result = capture(name, compile_fixture(name, fixture, target / 'release/libxp_ffi.a'))
                killed = result.returncode != 0 or result.stdout != expected
                record = {'test_name': test_name, 'mutant': name, 'kind': 'production-rust', 'source': str(path),
                          'original_source_sha256': digest(sources[path].encode()), 'mutant_source_sha256': digest(changed.encode()),
                          'returncode': result.returncode, 'trace_sha256': digest(result.stdout),
                          'stderr_sha256': digest(result.stderr), 'killed': killed}
                (evidence / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
                print(json.dumps(record), flush=True)
                assert killed, ('actual Rust mutant survived', name)
    finally:
        assert (HERE / golden_name).read_bytes() == golden, 'immutable golden changed'
        for path, before in hashes.items():
            assert digest((ROOT / path).read_bytes()) == before, ('source changed during test', path)


if __name__ == '__main__':
    main()
