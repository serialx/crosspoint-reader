"""Check real EPUB end-readiness publication before shared end-menu first paint."""
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
sys.path.insert(0, str(HERE))
from extract import definition as method

PRODUCTION = Path('crates/xp-ffi/src/app/epub/render_scope.rs')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    end_source = ROOT / 'src/activities/reader/EndOfBookOptions.cpp'
    end_bytes = end_source.read_bytes()
    end_method = method(end_bytes.decode(), 'EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(')
    template = (HERE / 'prepaint_input.cpp').read_text()
    assert template.count('// PREPAINT_METHODS') == 1
    fixture = template.replace('// PREPAINT_METHODS', end_method)
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    before = b'            state.base.end_ready = true;'
    after = b'            state.base.end_ready = false;'
    assert production.count(before) == 1, 'expected one mark_end_ready model publication'
    changed = production.replace(before, after, 1)
    fixture_hashes = {path.name: digest(path.read_bytes()) for path in (
        HERE / 'prepaint_input.cpp', HERE / 'base_owner_fixture.h',
        HERE / 'overlay_actions_session_fixture.h', Path(__file__),
        ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/base.rs')}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-prepaint-owner-') as directory:
            work = Path(directory)
            flags = rust_link_flags()

            def run(name, archive=None):
                path = work / (name + '.cpp')
                path.write_text(fixture)
                binary = work / name
                selected_flags = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(HERE), str(path), *selected_flags, '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('prepaint')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            copied_source = copied_rust / PRODUCTION
            assert copied_source.read_bytes() == production
            copied_source.write_bytes(changed)
            target = work / 'target'
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            failed = run('delayed', target / 'release/libxp_ffi.a')
            assert failed.returncode != 0, 'actual Rust readiness mutant survived'
            stderr = failed.stderr.decode(errors='replace')
            assert 'observed.end_ready' in stderr and 'beforeFirstPaint' in stderr, stderr
            print(json.dumps({
                'test_name': 'reader_prepaint_input', 'physical_button_cases': 8,
                'scope': 'real EpubSession render_base/mark_end_ready; actual shared handleMenuInput; physical End widget fake',
                'policy_replacements': ['BaseHooks.at_end=true', 'BaseHooks.end_rendered=noop',
                                        'BaseHooks.complete=noop'],
                'observer': 'BaseHooks.mark records call; actual Rust mark_page_rendered still runs',
                'fixture_sha256': fixture_hashes, 'native_end_source_sha256': digest(end_bytes),
                'native_end_extraction_sha256': digest(end_method.encode()),
                'candidate_source_sha256': digest(production), 'baseline_returncode': baseline.returncode,
                'mutant': 'mark-end-ready-model-publication-removed', 'kind': 'production-rust',
                'source': str(PRODUCTION), 'replacement_count': 1,
                'mutant_source_sha256': digest(changed), 'mutant_returncode': failed.returncode,
                'mutant_stderr_sha256': digest(failed.stderr),
                'mutant_assertion': 'beforeFirstPaint observed.end_ready',
            }), flush=True)
    finally:
        assert production_file.read_bytes() == production, 'real production source changed'
        assert end_source.read_bytes() == end_bytes, 'shared end widget source changed'
    print('prepaint: eight physical Back/Confirm/next/previous cases accepted; touch and remote refused; '
          'actual Rust first-paint readiness mutant rejected')


if __name__ == '__main__':
    main()
