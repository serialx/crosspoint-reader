"""Exercise the production owner render boundary during and after failed entry."""
from pathlib import Path
import argparse
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('blocked', 'failed'), required=True)
    args = parser.parse_args()
    scope = Path('crates/xp-ffi/src/app/epub/render_scope.rs')
    lifetime = Path('crates/xp-ffi/src/app/epub/lifetime.rs')
    if args.mode == 'blocked':
        path = scope
        before = ('pub(in crate::app) unsafe fn render(&self) -> bool {\n'
                  '        if !self.model(|state| state.ready) {')
        after = ('pub(in crate::app) unsafe fn render(&self) -> bool {\n'
                 '        if false {')
        mutant = 'render-entry-gate-removed'
        expected_assertion = 'value.ready'
    else:
        path = lifetime
        before = 'self.model(|state| state.ready = true);\n        loaded'
        after = 'self.model(|state| state.ready = loaded);\n        loaded'
        mutant = 'failed-entry-remains-unready'
        expected_assertion = 'a.ready == b.ready'
    original = (ROOT / 'rust' / path).read_text()
    assert original.count(before) == 1
    changed = original.replace(before, after, 1)
    with tempfile.TemporaryDirectory(prefix='reader-render-entry-') as directory:
        work = Path(directory)

        def compile_fixture(name, archive=None):
            binary = work / name
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-pthread',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer', '-I', str(HERE),
                            str(HERE / 'render_entry_gate.cpp'),
                            *rust_link_flags(archive=archive), '-o', str(binary)], check=True)
            return binary

        baseline = compile_fixture('baseline')
        subprocess.run([str(baseline), args.mode], check=True, timeout=30)
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust,
                        ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        (copied_rust / path).write_text(changed)
        target = work / 'target'
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        binary = compile_fixture(mutant, str(target / 'release/libxp_ffi.a'))
        failed = subprocess.run([str(binary), args.mode], capture_output=True,
                                text=True, timeout=30)
        assert failed.returncode != 0, (mutant, failed.stdout, failed.stderr)
        assert 'Assertion' in failed.stderr or 'assertion' in failed.stderr, failed.stderr
        assert expected_assertion in failed.stderr, failed.stderr
        print(json.dumps({'mode': args.mode, 'mutant': mutant, 'source': str(path),
                          'replacement_count': 1,
                          'original_sha256': hashlib.sha256(original.encode()).hexdigest(),
                          'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest(),
                          'returncode': failed.returncode, 'stderr': failed.stderr}), flush=True)
        assert (ROOT / 'rust' / path).read_text() == original
    print(f'render entry {args.mode}: real load/render boundary and actual Rust mutant pass')


if __name__ == '__main__':
    main()
