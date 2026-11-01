"""Compare real owner status/title/row methods with the immutable historical trace."""
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

TEST_NAME = 'reader_view_values'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/view_values.rs')
GZIP_SHA = '322ec60bff9fbb27a4f243d163e082aef2fbefce6a8ea28314162933786da48b'
TRACE_SHA = 'b72411265836af6d70604331abf6a7c09ff87b1b493f0be93bc2b2e2075cdfa1'
RUST_MUTANTS = (
    ('status-page-offset', b'self.model(|state| state.current.page).wrapping_add(1)',
     b'self.model(|state| state.current.page).wrapping_add(2)', False),
    ('low-memory-label', b'self.value_label(ids::STR_BT_LOW_MEMORY)',
     b'self.value_label(ids::STR_BT_FAILED)', False),
    ('point-size-unit', b'b" pt"', b'b" px"', False),
    ('real-action-label', b'    ids::STR_SELECT_CHAPTER,', b'    ids::STR_FOOTNOTES,', True),
    ('peer-name-fallback', b'if peer.name[0] != 0', b'if peer.name[0] == 0', True),
    ('connected-title-limit', b'bytes.len().min(title.len() - 1 - length)',
     b'bytes.len().min(title.len() - length)', True),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one actual source mutation', before, source.count(before))
    return source.replace(before, after, 1)


def candidate():
    if len(sys.argv) != 1:
        raise SystemExit('Expected no arguments, or the historical --freeze-original path.')
    golden = HERE / 'view-values.golden.gz'
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(expected) == TRACE_SHA
    counts = collections.Counter(line.split(b' ', 1)[0].decode() for line in expected.splitlines())
    assert len(expected.splitlines()) == 10776 and counts['case'] == 960 and counts['values'] == 14
    fixture_source = (HERE / 'view_values_oracle.cpp').read_bytes()
    fixture = replace_once(fixture_source, b'#include "view_values_session_rust.inc"',
                           (HERE / 'view_values_session_rust.inc').read_bytes())
    production_path = ROOT / 'rust' / PRODUCTION
    production = production_path.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'view_values_oracle.cpp', HERE / 'view_values_session_fixture.h',
        HERE / 'view_values_session_rust.inc', Path(__file__),
        HERE / 'test_reader_settings.py',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/view_values.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-view-values-session-') as directory:
            work = Path(directory)
            baseline_flags = rust_link_flags()

            def run(name, archive=None, real_labels=False):
                source = work / (name + '.cpp')
                source.write_bytes(fixture)
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                i18n = generated_i18n_directory(flags[-1])
                binary = work / name
                command = ['c++', '-std=c++17', '-UNDEBUG', '-O0', '-DVIEW_VALUES_OWNER=1',
                           '-fsanitize=address,undefined', '-I', str(HERE), '-I', str(i18n),
                           str(source), *flags, '-o', str(binary)]
                built = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (name + '.compile.log')).write_bytes(built.stdout + built.stderr)
                assert built.returncode == 0, (name, 'compile failure is not mutant rejection', built.stderr.decode(errors='replace'))
                argv = [str(binary)] + (['--real-facts'] if real_labels else [])
                result = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (work / (name + '.run.log')).write_bytes(result.stdout + result.stderr)
                return result

            baseline = run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            if baseline.stdout != expected:
                for index, (actual, original) in enumerate(zip(baseline.stdout.splitlines(), expected.splitlines()), 1):
                    assert actual == original, (index, original, actual)
                raise AssertionError(('trace length changed', len(expected), len(baseline.stdout)))
            real_labels = run('real-facts', real_labels=True)
            assert real_labels.returncode == 0, real_labels.stderr.decode(errors='replace')
            print(json.dumps({
                'test_name': TEST_NAME, 'status_title_cases': 960, 'row_setting_cases': 14,
                'trace_lines': 10776, 'trace_bytes': len(expected), 'event_counts': counts,
                'original_gzip_sha256': GZIP_SHA, 'original_trace_sha256': TRACE_SHA,
                'actual_trace_sha256': digest(baseline.stdout), 'candidate_source_sha256': digest(production),
                'fixture_sha256': fixture_hashes, 'real_action_labels': 19,
                'real_facts_output_sha256': digest(real_labels.stdout),
                'connected_extra_cases': ['long-name-63-byte-output', 'empty-name-address-fallback', 'percent-escape'],
                'policy_replacements': ['more_label: historical row labels are independently seeded STR_UNNAMED'],
                'observers': ['link_note_drawn: after actual owner state update'],
                'geometry_radio_settings': 'silent physical aggregate snapshots; no invented getter events',
                'scope': 'ordinary status/title/row methods under caller-held resource proof; no top-level render gate claim',
            }), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            target = work / 'target'
            for name, before, after, labels_only in RUST_MUTANTS:
                changed = replace_once(production, before, after)
                copied_source.write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result = run(name, target / 'release/libxp_ffi.a', labels_only)
                reference = real_labels.stdout if labels_only else expected
                assert result.returncode != 0 or result.stdout != reference, (name, 'actual Rust mutant survived')
                print(json.dumps({
                    'test_name': TEST_NAME, 'mutant': name, 'kind': 'production-rust', 'replacement_count': 1,
                    'source': str(PRODUCTION), 'original_source_sha256': digest(production),
                    'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                    'trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                    'historical_label_hook_enabled': not labels_only,
                }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert production_path.read_bytes() == production, 'real production source changed'
    print(f'{TEST_NAME}: 10776 exact lines; 19 real action labels; six actual Rust mutants rejected')


def historical_freeze():
    """Freeze original reader status, chapter titles and toolbar row values."""
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
    source = (subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
              if freeze else (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text())
    signatures = ['void EpubReaderActivity::renderStatusBar(', 'bool EpubReaderActivity::usesToolbarMenu(',
                  'std::string EpubReaderActivity::currentChapterTitle(', 'std::string EpubReaderActivity::textRowName(',
                  'std::string EpubReaderActivity::textRowValue(', 'std::string EpubReaderActivity::moreRowName(',
                  'std::string EpubReaderActivity::moreRowValue(']
    methods = '\n'.join(definition(source, signature) for signature in signatures)
    if not freeze:
        methods += '\n' + definition(source, 'std::string EpubReaderActivity::readerViewValue(')
    fixture = (HERE/'view_values_oracle.cpp').read_text().replace('// VALUE_METHODS', methods)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        def run(text):
            (path/'view.cpp').write_text(text)
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-fsanitize=address,undefined', str(path/'view.cpp'),
                            *rust_link_flags(), '-o', str(path/'view')], check=True)
            return subprocess.check_output([str(path/'view')])
        actual = run(fixture)
        golden = HERE/'view-values.golden.gz'
        if freeze:
            golden.write_bytes(gzip.compress(actual, mtime=0))
        else:
            assert actual == gzip.decompress(golden.read_bytes())
            for before, after in [('return o.section->currentPage;', 'return o.section->currentPage + 1;'),
                                  ('value = tr(STR_BT_LOW_MEMORY);', 'value = tr(STR_BT_FAILED);'),
                                  ('std::to_string(argument) + " pt"', 'std::to_string(argument) + " px"')]:
                wrong = fixture.replace(before, after)
                assert wrong != fixture and run(wrong) != actual
        print(f'view values: {len(actual.splitlines())} frozen lines across 960 status/title and 14 row-setting cases')

if __name__ == '__main__':
    if '--freeze-original' in sys.argv:
        historical_freeze()
    else:
        candidate()
