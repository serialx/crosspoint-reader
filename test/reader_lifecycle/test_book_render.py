"""Compare ordinary EpubSession book rendering with both immutable historical traces."""
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
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
from test_reader_settings import generated_i18n_directory

PRODUCTION = Path('crates/xp-ffi/src/app/epub/book_render.rs')
HISTORICAL_START = Path('test/reader_lifecycle/legacy_section_start_build.inc')
GOLDENS = {
    False: (5546, '7d18b43456f2247fce223a4b0b3bff491d661d50c39acf650c7096079894174c',
            '32e1c7195ca0ba72c9db32a378c3a6040226f47c57509b6de527c0a532e5801b'),
    True: (5106, '41c52ce22a2767fdaad4a020717519da4ebff2bf088cce21651da81053253352',
           '3bf81972a771861c6b31838021831e67f267141babe3846e10af4a4ae682d087'),
}
RUST_MUTANTS = (
    ('retry-increment', b'book_render_page',
     b'state.page_retries = state.page_retries.wrapping_add(1);', b'state.page_retries = 0;', False),
    ('pending-build-popup', b'book_initial_section',
     b'state.build_popup = !popup', b'state.build_popup = false', False),
    ('screenshot-consumption', b'book_notices',
     b'core::mem::take(&mut state.screenshot)', b'state.screenshot', False),
    ('section-publication', b'book_build_more',
     b'self.publish_section(access);', b'/* omitted Section publication */', False),
    ('page-before-section-close', b'book_render_page',
     b'self.drop_section(access);', b'drop(page); self.drop_section(access);', False),
    ('release-end-clock', b'book_render_page',
     b'if unsafe { (self.leaves.log_enabled)(0) } != 0 {', b'if true {', True),
)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one source anchor', before, source.count(before))
    return source.replace(before, after, 1)


def mutate_function(source, name, before, after):
    # Scope to one ordinary method, then require one exact production expression.
    marker = b'fn ' + name + b'('
    assert source.count(marker) == 1, (name, 'expected one method')
    start = source.index(marker)
    following = [source.find(marker, start + 1) for marker in (b'\n    fn ', b'\n    pub(super) fn ')]
    end = min([position for position in following if position >= 0] or [len(source)])
    body = source[start:end]
    return source[:start] + replace_once(body, before, after) + source[end:]


def candidate():
    assert sys.argv[1:] in ([], ['--release-log']), 'expected only --release-log'
    release = '--release-log' in sys.argv
    name = 'reader_book_render' + ('_release' if release else '')
    lines, gzip_sha, trace_sha = GOLDENS[release]
    golden = HERE / ('book-render-release.golden.gz' if release else 'book-render.golden.gz')
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == gzip_sha and digest(expected) == trace_sha
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in expected.splitlines())
    assert len(expected.splitlines()) == lines and counts['case'] == counts['state'] == counts['flags'] == 224
    source_path = ROOT / 'rust' / PRODUCTION
    production = source_path.read_bytes()
    native_path = ROOT / HISTORICAL_START
    native = native_path.read_bytes()
    fixture_template = replace_once((HERE / 'book_render_oracle.cpp').read_bytes(),
                           b'#include "book_render_session_rust.inc"',
                           (HERE / 'book_render_session_rust.inc').read_bytes())
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in (
        HERE / 'book_render_oracle.cpp', HERE / 'book_render_session_fixture.h',
        HERE / 'book_render_session_rust.inc', Path(__file__), HERE / 'test_reader_settings.py', HERE / 'test_popup_resource.py',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/book_render.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix=name + '-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def run(label, archive=None, native_source=native):
                cpp = work / (label + '.cpp')
                fixture = replace_once(fixture_template, b'/* ACTUAL_SECTION_START_BUILD */',
                                       native_source)
                cpp.write_bytes(fixture)
                binary = work / label
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                i18n = generated_i18n_directory(flags[-1])
                command = ['c++', '-std=c++17', '-UNDEBUG', '-O0', '-Wno-format',
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DBOOK_RENDER_OWNER=1',
                           *(['-DBOOK_RELEASE_LOG'] if release else []),
                           '-I', str(HERE), '-I', str(i18n), str(cpp), *flags, '-o', str(binary)]
                built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (label + '.compile.log')).write_bytes(built.stdout + built.stderr)
                assert built.returncode == 0, (label, 'compile failure is not mutant rejection', built.stderr.decode(errors='replace'))
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
                (work / (label + '.run.log')).write_bytes(result.stdout + result.stderr)
                return result

            baseline = run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            if baseline.stdout != expected:
                for index, (actual, original) in enumerate(zip(baseline.stdout.splitlines(), expected.splitlines()), 1):
                    assert actual == original, (index, original, actual)
                raise AssertionError(('trace length changed', len(expected), len(baseline.stdout)))
            print(json.dumps({
                'test_name': name, 'cases': 224, 'trace_lines': lines, 'trace_bytes': len(expected),
                'original_gzip_sha256': gzip_sha, 'original_trace_sha256': trace_sha,
                'actual_trace_sha256': digest(baseline.stdout), 'event_counts': counts,
                'candidate_source_sha256': digest(production), 'historical_shim_sha256': digest(native), 'fixture_sha256': hashes,
                'policy_replacements': ['settle overlay', 'build room', 'apply deferred', 'status bar',
                    'bookmark flag', 'render contents', 'save progress', 'toolbar menu', 'render overlay', 'push refresh'],
                'observers_after_real_mutation': ['clear deferred', 'discard overlay', 'mark rendered'],
                'real_inner_method': 'show_build_popup',
                'publication': 'five physical facts in order after open/load/create/start/build-more, including false returns; cleared before native close',
                'scope': 'render_book under render_with and caller-held RenderLock; historical inner replacements are not new inner coverage',
                'page_lifetime': 'owned Page closes after contents; failed metadata retains Page until Section/error cleanup',
                'geometry': 'separate margins callback; cheap scalar snapshot includes const-map TTF presence and fixed UI font',
            }), flush=True)
            if not release:
                before = b'if (!popup) return section(handle).startBuild(renderSpec(*spec));'
                after = b'if (!popup) return section(handle).startBuild(renderSpec(*spec), [] {});'
                wrong_native = replace_once(native, before, after)
                result = run('native-null-popup', native_source=wrong_native)
                assert result.returncode != 0 or result.stdout != expected, 'native null-popup mutant survived'
                print(json.dumps({
                    'test_name': name, 'mutant': 'native-null-popup', 'kind': 'historical-fixture',
                    'replacement_count': 1, 'source': str(HISTORICAL_START), 'original_source_sha256': digest(native),
                    'mutant_source_sha256': digest(wrong_native), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                }), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for dependency in ('scripts', 'lib', 'src'):
                (copied_repo / dependency).symlink_to(ROOT / dependency, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            mutants = [item for item in RUST_MUTANTS if item[4] == release]
            assert mutants, 'each registered test must reject an actual production mutant'
            for label, function, before, after, _ in mutants:
                changed = mutate_function(production, function, before, after)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result = run(label, target / 'release/libxp_ffi.a')
                assert result.returncode != 0 or result.stdout != expected, (label, 'actual Rust mutant survived')
                print(json.dumps({
                    'test_name': name, 'mutant': label, 'kind': 'production-rust', 'replacement_count': 1,
                    'source': str(PRODUCTION), 'function': function.decode(), 'original_source_sha256': digest(production),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert source_path.read_bytes() == production, 'real production source changed'
        assert native_path.read_bytes() == native, 'historical shim changed'
    print(f'{name}: {lines} exact lines across 224 build/load/notice cases; actual Rust mutants rejected')


def historical_freeze():
    """Freeze section build, page restore and reader post-render resource ordering."""
    from pathlib import Path
    import gzip
    import subprocess
    import sys
    import tempfile
    ROOT = Path(__file__).resolve().parents[2]
    HERE = Path(__file__).resolve().parent
    sys.path.insert(0, str(HERE))
    from extract import definition
    sys.path.insert(0, str(ROOT / 'test'))
    from rust_host import rust_link_flags
    freeze = '--freeze-original' in sys.argv
    release = '--release-log' in sys.argv
    source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
              if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
    fixture = (HERE / 'book_render_oracle.cpp').read_text().replace('// BOOK_METHOD', definition(source, 'void EpubReaderActivity::renderBook()'))
    with tempfile.TemporaryDirectory() as directory:
        p=Path(directory)
        def run(text, name):
            (p/(name+'.cpp')).write_text(text)
            subprocess.run(['c++','-std=c++17','-UNDEBUG','-Wno-format','-fsanitize=address,undefined',str(p/(name+'.cpp')),
                            *(['-DBOOK_RELEASE_LOG'] if release else []),*rust_link_flags(),'-o',str(p/name)],check=True)
            return subprocess.check_output([str(p/name)],timeout=20)
        actual=run(fixture,'book')
        golden=HERE/('book-render-release.golden.gz' if release else 'book-render.golden.gz')
        if freeze: golden.write_bytes(gzip.compress(actual,mtime=0))
        else:
            assert actual==gzip.decompress(golden.read_bytes())
            for before,after in [('o.pageLoadRetryCount = static_cast<decltype(o.pageLoadRetryCount)>(value);','o.pageLoadRetryCount = 0;'),
                                 ('o.buildPopupPending = static_cast<decltype(o.buildPopupPending)>(value);','o.buildPopupPending = false;'),
                                 ('o.pendingScreenshot = static_cast<decltype(o.pendingScreenshot)>(value);','o.pendingScreenshot = true;')]:
                wrong=fixture.replace(before,after)
                assert wrong!=fixture
                assert run(wrong,'wrong')!=actual
        print(f'book render ({"release" if release else "debug"}): {len(actual.splitlines())} frozen lines across 224 build/load/notice cases')


if __name__ == "__main__":
    if "--freeze-original" in sys.argv:
        historical_freeze()
    else:
        candidate()
