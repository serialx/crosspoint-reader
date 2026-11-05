"""Reject stale panel installation while preserving Back cancellation after failure."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    path = Path('crates/xp-ffi/src/app/epub/overlay_actions.rs')
    original = (ROOT / 'rust' / path).read_text()
    before = ('if self.model(|state| state.scene_gate.failed) {\n'
              '                unsafe { (self.leaves.toolbar_close)(toolbar.as_ptr()) };')
    after = ('if false {\n'
             '                unsafe { (self.leaves.toolbar_close)(toolbar.as_ptr()) };')
    assert original.count(before) == 1
    with tempfile.TemporaryDirectory(prefix='reader-overlay-install-') as directory:
        work = Path(directory)

        def compile_fixture(name, archive=None, fixture='overlay_install_rejection.cpp'):
            binary = work / name
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-pthread', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all', '-I', str(HERE),
                            str(HERE / fixture), *rust_link_flags(archive=archive),
                            '-o', str(binary)], check=True)
            return binary

        subprocess.run([str(compile_fixture('baseline'))], check=True, timeout=30)
        subprocess.run([str(compile_fixture('cancel-baseline', fixture='overlay_cancel_rejection.cpp'))],
                       check=True, timeout=30)
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        changed = original.replace(before, after, 1)
        (copied_rust / path).write_text(changed)
        target = work / 'target'
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        failed = subprocess.run([str(compile_fixture('missing-recheck', str(target / 'release/libxp_ffi.a')))],
                                capture_output=True, text=True, timeout=30)
        assert failed.returncode != 0 and 'late rejection must not open the panel' in failed.stderr, failed.stderr
        print(json.dumps({'mutant': 'missing-install-recheck', 'returncode': failed.returncode,
                          'source': str(path), 'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                          'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest()}), flush=True)
        cancel_before = 'if input.released & BACK != 0 {'
        cancel_after = 'if input.released & BACK != 0 && !self.model(|state| state.scene_gate.failed) {'
        assert original.count(cancel_before) == 1
        changed = original.replace(cancel_before, cancel_after, 1)
        (copied_rust / path).write_text(changed)
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        failed = subprocess.run([str(compile_fixture('cancel-blocked-by-failure',
                                str(target / 'release/libxp_ffi.a'), 'overlay_cancel_rejection.cpp'))],
                                capture_output=True, text=True, timeout=30)
        assert failed.returncode != 0 and ('Back cancellation order' in failed.stderr
                                          or 'failed Back' in failed.stderr), failed.stderr
        print(json.dumps({'mutant': 'cancel-blocked-by-failure', 'returncode': failed.returncode,
                          'source': str(path), 'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                          'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest()}), flush=True)
    assert (ROOT / 'rust' / path).read_text() == original


if __name__ == '__main__':
    main()
