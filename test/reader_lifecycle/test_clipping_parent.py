"""Compare real clipping owner policy with immutable debug and release traces."""
import collections
import gzip
import hashlib
import json
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

PRODUCTION = Path('crates/xp-ffi/src/app/epub/clipping.rs')
GOLDENS = {
    False: ('4acc999b45b0ae09dadaeec7a26ccd46183b670eb9054c5fc4b8818f4db2c014',
            '2d302b973e37b8163e16ab7797e53a6a83ce4b75e2cb5d5301e9eb9d0fb10164', 4151887, 284819),
    True: ('ca219e3f4c483c38aeb28ae792414d6029aeb182c52e45ba7a960f48d55fdf6a',
           '72e6ac0a9873765cfeda881b066955839b3c5b39d74b5e1ad0de3c3a5eb82ee4', 4148095, 284723),
}
DRAW = b'pub(super) fn draw_clipping_highlights('
# The draw scope disambiguates two intentionally identical hit/draw filters.
MUTANTS = (
    ('zero-width-consumes-word', DRAW, b'if width <= 0 {', b'if width < 0 {'),
    ('pixel-column-alignment', None, b'let mut x = rect.x.wrapping_add(rect.x & 1);', b'let mut x = rect.x;'),
    ('inclusive-source-overlap', None, b'end > row.start_offset', b'end >= row.start_offset'),
    ('invisible-word-eager-metrics', DRAW,
     b'''let word = page.word(element, index);
                if !visible(&word) {
                    continue;
                }
                let word_font = page.word_font(element, index, font);
                let width = page.width(&word, word_font);''',
     b'''let word = page.word(element, index);
                let word_font = page.word_font(element, index, font);
                let width = page.width(&word, word_font);
                if !visible(&word) {
                    continue;
                }'''),
    ('eager-bw-query', None,
     b'''if unsafe { read(services.context, Query::IsA4 as u8) } != 0
            && unsafe { read(services.context, Query::IsBw as u8) } == 0''',
     b'''let a4 = unsafe { read(services.context, Query::IsA4 as u8) };
        let bw = unsafe { read(services.context, Query::IsBw as u8) };
        if a4 != 0 && bw == 0'''),
    ('release-end-clock', None,
     b'''if unsafe { (self.leaves.log_enabled)(0) } != 0 {
            let elapsed = unsafe { (self.runtime.now_ms)() }.wrapping_sub(started);''',
     b'''let elapsed = unsafe { (self.runtime.now_ms)() }.wrapping_sub(started);
        if unsafe { (self.leaves.log_enabled)(0) } != 0 {'''),
    ('real-signature-font-omitted', None, b'spec.font as u32,', b'0, /* deliberate font omission */'),
    ('real-count-singleton-empty', None,
     b'let nonempty = unsafe { (self.leaves.clippings_count)() } != 0;',
     b'let nonempty = unsafe { (self.leaves.clippings_count)() } > 1;'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('replacement must match one site', before)
    return source.replace(before, after, 1)


def mutate(source, scope, before, after):
    offset = 0
    if scope is not None:
        assert source.count(scope) == 1, ('mutation scope must be unique', scope)
        offset = source.index(scope)
    return source[:offset] + replace_once(source[offset:], before, after)


def freeze_original(release):
    # Preserve the historical capture entry verbatim in behavior; candidate runs never enter it.
    def source(path):
        return subprocess.check_output(['git', 'show', '8d534a99:' + path], cwd=ROOT, text=True)
    reader = source('src/activities/reader/EpubReaderActivity.cpp')
    clipping = source('src/ClippingStore.h')
    methods = '\n'.join(definition(reader, s) for s in [
        'int EpubReaderActivity::clippingAtPoint(', 'void EpubReaderActivity::drawClippingHighlights('])
    geometry = '\n'.join(definition(reader, s) for s in ['int clippingWordFont(', 'Rect clippingWordRect('] if s in reader)
    if 'int EpubReaderActivity::runClippingPolicy(' in reader:
        methods += '\n' + definition(reader, 'int EpubReaderActivity::runClippingPolicy(')
    match = '\n'.join(definition(clipping, s) for s in [
        'inline bool clippingStoredRangeMatchesLayout(', 'inline bool clippingContainsWord('])
    fixture = (HERE / 'clipping_parent_oracle.cpp').read_text().replace(
        '// CLIPPING_RECORD', next(line for line in clipping.splitlines() if line.startswith('inline constexpr size_t CLIPPING_CHAPTER_TITLE_MAX'))
        + '\n' + definition(clipping, 'struct Clipping {') + ';').replace('// CLIPPING_MATCH', match).replace(
        '// GEOMETRY_METHODS', geometry).replace('// CLIPPING_METHODS', methods)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        def run(text):
            (path / 'clipping.cpp').write_text(text)
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', '-I' + str(ROOT / 'src'),
                            *(['-DCLIPPING_RELEASE_LOG'] if release else []), str(path / 'clipping.cpp'),
                            *rust_link_flags(), '-o', str(path / 'clipping')], check=True)
            return subprocess.check_output([str(path / 'clipping')])
        actual = run(fixture)
        golden = HERE / f'clipping-parent{"-release" if release else ""}.golden.gz'
        golden.write_bytes(gzip.compress(actual, mtime=0))
        for before, after in [('if (width <= 0) continue;', 'if (width < 0) continue;'),
                              ('x + (x & 1)', 'x'),
                              ('sourceEnd > clipping.startOffset', 'sourceEnd >= clipping.startOffset')]:
            wrong = fixture.replace(before, after)
            assert wrong != fixture and run(wrong) != actual, (before, release)
        print(f'clipping parents release={release}: {len(actual.splitlines())} original trace lines')


def main():
    release = '--release-log' in sys.argv
    if '--freeze-original' in sys.argv:
        freeze_original(release)
        return
    test_name = 'reader_clipping_parent' + ('_release' if release else '')
    compressed = {}
    expected = {}
    golden_paths = {}
    for mode in (False, True):
        golden_paths[mode] = HERE / f'clipping-parent{"-release" if mode else ""}.golden.gz'
        compressed[mode] = golden_paths[mode].read_bytes()
        expected[mode] = gzip.decompress(compressed[mode])
        gzip_sha, trace_sha, size, lines = GOLDENS[mode]
        assert digest(compressed[mode]) == gzip_sha and digest(expected[mode]) == trace_sha
        assert len(expected[mode]) == size and len(expected[mode].splitlines()) == lines
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in expected[release].splitlines())
    assert counts['case'] == counts['has-clips'] == 144 and counts['hit'] == 3024
    assert counts['element'] == 12192 and counts['a4'] == 198 and counts['mode'] == 99
    assert counts['clock'] == (48 if release else 96) and counts['debug'] == (0 if release else 48)
    clipping = (ROOT / 'src/ClippingStore.h').read_text()
    fixture = (HERE / 'clipping_parent_oracle.cpp').read_text()
    fixture = replace_once(fixture, '// CLIPPING_RECORD',
        next(line for line in clipping.splitlines() if line.startswith('inline constexpr size_t CLIPPING_CHAPTER_TITLE_MAX'))
        + '\n' + definition(clipping, 'struct Clipping {') + ';')
    fixture = replace_once(fixture, '// CLIPPING_MATCH', '')
    fixture = replace_once(fixture, '// GEOMETRY_METHODS', '')
    fixture = replace_once(fixture, '// CLIPPING_METHODS', '#include "clipping_owner_rust.inc"')
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'clipping_parent_oracle.cpp', HERE / 'clipping_owner_fixture.h', HERE / 'clipping_owner_rust.inc',
        Path(__file__), ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/clipping.rs',
        ROOT / 'lib/Epub/Epub/ReaderRenderSpec.h')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-clipping-owner-') as directory:
            work = Path(directory)
            source_path = work / 'clipping.cpp'
            source_path.write_text(fixture)
            baseline_flags = rust_link_flags()

            def compile_candidate(name, mode, archive=None):
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                executable = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-DXP_CLIPPING_OWNER', *(['-DCLIPPING_RELEASE_LOG'] if mode else []),
                                '-I', str(HERE), '-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/Epub'),
                                str(source_path), *flags, '-o', str(executable)], check=True)
                return executable

            def run(executable, *args):
                return subprocess.run([str(executable), *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            executable = compile_candidate('clipping-owner', release)
            baseline = run(executable)
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == expected[release], (digest(baseline.stdout), GOLDENS[release][1])
            facts = run(executable, '--real-facts')
            assert facts.returncode == 0, facts.stderr.decode(errors='replace')
            assert facts.stdout == b'clipping real facts: 17 signatures / 68 counts; hooks disabled\n'
            print(json.dumps({
                'test_name': test_name, 'cases': 144, 'hits': 3024, 'release_log': release,
                'trace_lines': GOLDENS[release][3], 'trace_bytes': GOLDENS[release][2],
                'original_gzip_sha256': GOLDENS[release][0], 'original_trace_sha256': GOLDENS[release][1],
                'actual_trace_sha256': digest(baseline.stdout), 'event_counts': counts,
                'candidate_source_sha256': digest(production), 'fixture_sha256': fixture_hashes,
                'real_signature_cases': 17, 'real_count_cases': 68, 'real_facts_trace_sha256': digest(facts.stdout),
                'historical_policy_replacement': 'signature=123 only; disabled in real-facts mode',
                'historical_observer': 'nonempty checks real count result and emits has-clips',
                'element_observation': 'one standalone enumeration event; physical metric view reconstruction is silent',
                'resource_scope': 'silent existing caller lock; Page remains borrowed; Section published then dropped once',
            }), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            selected = [item for item in MUTANTS if not release or item[0] == 'release-end-clock']
            for name, scope, before, after in selected:
                changed = mutate(production, scope, before, after)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                mode = True if name == 'release-end-clock' else release
                mutant_executable = compile_candidate(name, mode, target / 'release/libxp_ffi.a')
                result = run(mutant_executable)
                real_result = run(mutant_executable, '--real-facts')
                rejected_trace = result.returncode != 0 or result.stdout != expected[mode]
                rejected_facts = real_result.returncode != 0 or real_result.stdout != facts.stdout
                assert rejected_trace or rejected_facts, (name, 'actual Rust mutant survived')
                if name.startswith('real-'):
                    assert rejected_facts, (name, 'real-facts checks did not reject mutant')
                print(json.dumps({
                    'test_name': test_name, 'mutant': name, 'kind': 'production-rust', 'replacement_count': 1,
                    'scope': scope.decode() if scope else 'whole file', 'release_log': mode,
                    'source': str(PRODUCTION), 'original_source_sha256': digest(production),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                    'real_facts_returncode': real_result.returncode, 'real_facts_sha256': digest(real_result.stdout),
                    'real_facts_stderr_sha256': digest(real_result.stderr),
                }), flush=True)
    finally:
        for mode in (False, True):
            assert golden_paths[mode].read_bytes() == compressed[mode], 'immutable original golden changed'
        assert production_file.read_bytes() == production, 'real production source changed'
    print(f'{test_name}: 144 cases / 3024 hits; real signature/count checks; {len(selected)} actual Rust mutants rejected')


if __name__ == '__main__':
    main()
