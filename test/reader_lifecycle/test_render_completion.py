"""Exercise the actual owner completion against model-locked transitions."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

source = Path(os.environ.get('XP_READER_GATE_SOURCE', ROOT))
path = Path('crates/xp-ffi/src/app/epub/render_scope.rs')
original = (source / 'rust' / path).read_text()
before = """        self.model(|state| {
            if matches!(state.overlay, Overlay::Page) {
                state.scene_gate.failed = false;
            }
        });"""
after = """        let publish = self.model(|state| matches!(state.overlay, Overlay::Page));
        if publish {
            self.model(|state| state.scene_gate.failed = false);
        }"""
assert original.count(before) == 1
with tempfile.TemporaryDirectory(prefix='reader-completion-') as directory:
    out = Path(directory)
    def compile_fixture(name, archive=None):
        binary = out / name
        subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I'+str(HERE), str(HERE/'render_completion.cpp'),
                        *rust_link_flags(archive=archive), '-o', str(binary)], check=True)
        return binary
    subprocess.run([str(compile_fixture('completion'))], check=True, timeout=30)
    copied_repo = out / 'source'
    copied_rust = copied_repo / 'rust'
    shutil.copytree(source / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
    for name in ('scripts', 'lib', 'src'):
        (copied_repo / name).symlink_to(source / name, target_is_directory=True)
    changed = original.replace(before, after, 1)
    (copied_rust / path).write_text(changed)
    target = out / 'target'
    subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                    '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                    '--features', 'app-test'], cwd=copied_rust, check=True)
    result = subprocess.run([str(compile_fixture('split', str(target/'release/libxp_ffi.a')))],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode != 0 and 'completion must publish before model unlock' in result.stderr, result.stderr
    print(json.dumps({'mutant': 'split-completion-decision-publication', 'returncode': result.returncode,
                      'source': str(path), 'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                      'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest()}), flush=True)
assert (source / 'rust' / path).read_text() == original
print('actual owner completion transitions pass; split decision/publication mutant rejected')
