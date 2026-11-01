"""Retain the production build-popup callback across real owner render frames.

The fake Section stores the production pointer and owner context. Registration
into native Section is covered separately by the book-render oracle/A1; this
fixture does not claim a complete actual Section lifetime.
"""
import hashlib
import json
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

PRODUCTION = Path('crates/xp-ffi/src/app/epub/book_render.rs')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def generated_i18n_directory(archive):
    headers = sorted((Path(archive).resolve().parent / 'build').glob('xp-core-*/out/I18nKeys.h'))
    assert headers, ('missing generated I18nKeys.h', str(archive))
    assert len({path.read_bytes() for path in headers}) == 1, 'conflicting generated I18n headers'
    return headers[0].parent


def main():
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    before = (b'pub(super) unsafe extern "C" fn build_popup(context: *mut c_void) {\n'
              b'    let owner = &*context.cast::<EpubSession>();\n'
              b'    let access = owner.borrowed_resources();\n'
              b'    owner.show_build_popup(&access);\n'
              b'}')
    after = before.replace(b'    owner.show_build_popup(&access);',
                           b'    let _ = owner.render_refresh_counter(&access);\n'
                           b'    owner.show_build_popup(&access);')
    assert production.count(before) == 1, 'expected actual build_popup callback definition'
    changed = production.replace(before, after, 1)
    fixture_path = HERE / 'build_popup_lifetime.cpp'
    fixture = fixture_path.read_bytes()
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        fixture_path, HERE / 'overlay_actions_session_fixture.h', Path(__file__),
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/overlay_actions.rs',
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/publication.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-build-popup-owner-') as directory:
            work = Path(directory)
            flags = rust_link_flags()

            def run(name, archive=None):
                path = work / (name + '.cpp')
                path.write_bytes(fixture)
                binary = work / name
                link_flags = flags if archive is None else rust_link_flags(archive=str(archive))
                selected_archive = flags[-1] if archive is None else archive
                i18n = generated_i18n_directory(selected_archive)
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', '-I', str(HERE), '-I', str(i18n),
                                str(path), *link_flags, '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('popup')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout == (b'retained build popup: three real callback cases; frame expired; '
                                       b'same owner context\n')
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            copied_source.write_bytes(changed)
            target = work / 'target'
            # A compilation failure does not count as rejecting this mutant.
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            failed = run('eager-frame-read', target / 'release/libxp_ffi.a')
            assert failed.returncode != 0, 'actual Rust eager-frame mutant survived'
            stderr = failed.stderr.decode(errors='replace')
            # The no_std host panic handler aborts without printing the expect message.
            # The marker is immediately before invoking the retained no-frame callback.
            assert failed.returncode == -signal.SIGABRT, (failed.returncode, stderr)
            assert stderr.strip() == 'background no-frame callback', stderr
            print(json.dumps({
                'test_name': 'reader_build_popup_lifetime', 'retained_callback_cases': 3,
                'scope': 'actual production callback pointer and owner; real render_with; fake retaining Section',
                'limitation': 'native Section callback registration/lifetime remains separate book-render oracle/A1 coverage',
                'policy_replacements': [], 'resource_contract': 'caller RenderLock depth1, ModelLock depth0 at leaves',
                'fixture_sha256': fixture_hashes, 'candidate_source_sha256': digest(production),
                'baseline_returncode': baseline.returncode, 'baseline_stdout_sha256': digest(baseline.stdout),
                'mutant': 'eager-callback-render-counter', 'kind': 'production-rust', 'source': str(PRODUCTION),
                'replacement_count': 1, 'mutant_source_sha256': digest(changed),
                'mutant_returncode': failed.returncode, 'mutant_stderr_sha256': digest(failed.stderr),
            }), flush=True)
    finally:
        assert production_file.read_bytes() == production, 'real production source changed'
        assert fixture_path.read_bytes() == fixture, 'fixture source changed'
    print('retained build popup: active frame, background probe, unavailable framebuffer; actual Rust eager lookup rejected')


if __name__ == '__main__':
    main()
