"""Retain shared/base input coverage and add real EPUB book-touch/menu coverage."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

TEST_NAME = 'reader_base_input_wrappers'
GZIP_SHA = '26a897febf037897c3e64bddc52df23ddc2a7fd5dbf0cf96c83935ff7b475199'
RAW_SHA = '0c333dfafcfa4a9aa1b2ad924d855e09843d74a392b3591d1704a07b11473175'
BASE_SOURCE = Path('crates/xp-ffi/src/app/epub/base.rs')
EPUB_SOURCE = Path('crates/xp-ffi/src/app/epub/loop_input.rs')
BASE_MUTANTS = (
    ('wrong-long-button-setting', b'long_press: settings.long_button,',
     b'long_press: settings.short_power,'),
    ('wrong-ltr-direction',
     b'geometry.width,\n                geometry.height,\n                false,',
     b'geometry.width,\n                geometry.height,\n                true,'),
)
EPUB_MUTANTS = (
    ('inverted-book-language-direction',
     b'input::language_rtl(unsafe { language.bytes() }.unwrap_or_default())',
     b'!input::language_rtl(unsafe { language.bytes() }.unwrap_or_default())'),
    ('wrong-menu-gesture-setting', b'self.settings(false).show_menu,', b'0,'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run(executable):
    return subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)


def require_trace(result, expected, label):
    assert result.returncode == 0, (label, result.returncode, result.stderr.decode(errors='replace'))
    if result.stdout != expected:
        actual_lines, expected_lines = result.stdout.splitlines(), expected.splitlines()
        mismatch = next((index for index, pair in enumerate(zip(actual_lines, expected_lines), 1)
                         if pair[0] != pair[1]), min(len(actual_lines), len(expected_lines)) + 1)
        raise AssertionError((label, 'trace mismatch', mismatch, digest(result.stdout), digest(expected)))


def replace_once(source, before, after):
    assert source.count(before) == 1, ('fixture anchor must match once', before)
    return source.replace(before, after, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('shared_executable', nargs='?', type=Path)
    parser.add_argument('--epub-only', action='store_true',
                        help='run only the EPUB candidate and its two mutants')
    parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2],
                        help='repository root; required when running the external draft directly')
    args = parser.parse_args()
    if not args.epub_only and args.shared_executable is None:
        parser.error('SHARED_READER_INPUT_ORACLE is required unless --epub-only is selected')
    root = args.repo.resolve()
    here = root / 'test/reader_lifecycle'
    adapter = Path(__file__).resolve().with_name('input_session_rust.inc')
    sys.path.insert(0, str(root / 'test'))
    from rust_host import rust_link_flags

    golden = here / 'input.golden.gz'
    compressed = golden.read_bytes()
    expected = gzip.decompress(compressed)
    assert digest(compressed) == GZIP_SHA and digest(expected) == RAW_SHA
    sources = {path: (root / 'rust' / path).read_bytes() for path in (BASE_SOURCE, EPUB_SOURCE)}
    fixture = here / 'input_oracle.cpp'
    fixture_bytes = fixture.read_bytes()
    fixture_source = fixture_bytes.decode()
    assert '"' not in str(adapter)
    base_fixture = replace_once(fixture_source, '#include "input_session_rust.inc"',
                                '#include "' + str(adapter) + '"')
    epub_fixture = replace_once(
        base_fixture,
        'auto r = rtl ? ReaderUtils::detectTouchPageTurn(renderer, input, rtl) : ownerTouch(input);',
        'auto r = ownerBookTouch(input, rtl);')
    epub_fixture = replace_once(
        epub_fixture, 'const bool open = ReaderUtils::isTouchMenuGesture(renderer, input);',
        'const bool open = ownerMenu(input);')
    evidence_files = (
        fixture, adapter, here / 'overlay_actions_session_fixture.h',
        here / 'extract_input.py', here / 'extract.py', Path(__file__).resolve(),
        root / 'src/activities/reader/ReaderUtils.h',
        here / 'reader_value_owner.h',
        root / 'test/ble_link_note_title/owner_tables.h',
        here / 'projection_session_fixture.h',
        root / 'rust/crates/xp-ffi/src/app/epub/test_support/input.rs',
        root / 'rust/crates/xp-ffi/include/xp_epub_session.h',
    )
    fixture_hashes = {str(path): digest(path.read_bytes()) for path in evidence_files}
    modes = ('epub',) if args.epub_only else ('base', 'epub')
    shared = None
    try:
        if not args.epub_only:
            shared = run(args.shared_executable.resolve())
            require_trace(shared, expected, 'shared original')
        with tempfile.TemporaryDirectory(prefix='reader-input-session-') as directory:
            work = Path(directory)
            extracted = work / 'input_methods.inc'
            subprocess.run([sys.executable, str(here / 'extract_input.py'), str(root), str(extracted)], check=True)
            fixture_paths = {}
            for mode, text in (('base', base_fixture), ('epub', epub_fixture)):
                fixture_paths[mode] = work / ('input-' + mode + '.cpp')
                fixture_paths[mode].write_text(text)
            baseline_flags = rust_link_flags()

            def compile_fixture(name, mode, archive=None):
                flags = baseline_flags if archive is None else rust_link_flags(archive=str(archive))
                executable = work / name
                subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-DXP_INPUT_SESSION=1', *(['-DXP_INPUT_EPUB=1'] if mode == 'epub' else []),
                                '-I', str(work), '-I', str(here),
                                '-I', str(root / 'src/activities/reader'), str(fixture_paths[mode]),
                                *flags, '-o', str(executable)], check=True)
                return executable

            for mode in modes:
                actual = run(compile_fixture('input-owner-' + mode, mode))
                require_trace(actual, expected, 'owner ' + mode)
                source_path = BASE_SOURCE if mode == 'base' else EPUB_SOURCE
                print(json.dumps({
                    'test_name': TEST_NAME, 'candidate_mode': mode,
                    'scope': ('real owner buttons and base LTR touch' if mode == 'base' else
                              'real owner buttons, book LTR/RTL touch and touch menu gesture'),
                    'pending_scope': 'Back remains shared; real App A1 routing remains pending',
                    'button_cases': 63360, 'owner_button_calls': 126720,
                    'owner_ltr_touch_calls': 6750 if mode == 'base' else 0,
                    'shared_rtl_touch_cases': 6750 if mode == 'base' else 0,
                    'owner_book_touch_calls': 13500 if mode == 'epub' else 0,
                    'owner_menu_calls': 13500 if mode == 'epub' else 0,
                    'book_language_callbacks': 13500 if mode == 'epub' else 0,
                    'shared_back_cases': 48, 'original_gzip_sha256': GZIP_SHA,
                    'original_raw_sha256': RAW_SHA, 'original_gzip_immutable': True,
                    'shared_trace_sha256': digest(shared.stdout) if shared is not None else None,
                    'owner_trace_sha256': digest(actual.stdout), 'trace_bytes': len(expected),
                    'trace_lines': len(expected.splitlines()), 'candidate_source': str(source_path),
                    'candidate_source_sha256': digest(sources[source_path]),
                    'transformed_fixture_sha256': digest(fixture_paths[mode].read_bytes()),
                    'extracted_methods_sha256': digest(extracted.read_bytes()), 'fixture_sha256': fixture_hashes,
                }), flush=True)

            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(root / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(root / name, target_is_directory=True)
            for path, source in sources.items():
                assert (copied_rust / path).read_bytes() == source
            target = work / 'target'
            for mode in modes:
                source_path = BASE_SOURCE if mode == 'base' else EPUB_SOURCE
                source = sources[source_path]
                mutants = BASE_MUTANTS if mode == 'base' else EPUB_MUTANTS
                for name, before, after in mutants:
                    # Each mutation starts from pristine copies of both production files.
                    for path, original in sources.items():
                        (copied_rust / path).write_bytes(original)
                    assert source.count(before) == 1, (name, 'mutation site must match once')
                    changed = source.replace(before, after, 1)
                    (copied_rust / source_path).write_bytes(changed)
                    subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                    '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                    '--features', 'app-test'], cwd=copied_rust, check=True)
                    result = run(compile_fixture(name, mode, target / 'release/libxp_ffi.a'))
                    # Compilation or crashes do not count: policy must run and change the trace.
                    assert result.returncode == 0, (name, result.returncode, result.stderr.decode(errors='replace'))
                    assert result.stdout != expected, (name, 'mutant survived')
                    print(json.dumps({
                        'test_name': TEST_NAME, 'candidate_mode': mode, 'mutant': name, 'replacement_count': 1,
                        'source': str(source_path), 'original_source_sha256': digest(source),
                        'mutant_source_sha256': digest(changed), 'returncode': result.returncode,
                        'mutant_trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                        'expected_trace_sha256': RAW_SHA,
                    }), flush=True)
    finally:
        assert golden.read_bytes() == compressed, 'immutable original golden changed'
        assert fixture.read_bytes() == fixture_bytes, 'original input fixture changed'
        for path, source in sources.items():
            assert (root / 'rust' / path).read_bytes() == source, 'real production source changed'
    print(f'{TEST_NAME}: modes {",".join(modes)} passed with two actual Rust mutants per mode; '
          'Back remains shared and real App A1 routing remains pending')


if __name__ == '__main__':
    main()
