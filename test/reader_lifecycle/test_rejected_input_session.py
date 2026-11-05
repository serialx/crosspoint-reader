"""Verify the exact physical sampler and real owner failed-scene admission."""
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

path = Path('crates/xp-ffi/src/app/epub/rejected_input.rs')
original = (ROOT / 'rust' / path).read_text()
helper_path = ROOT / 'src/components/RustInputSnapshot.h'
helper = helper_path.read_text()
assert 'XpUiInput RustActivityAdapter::sampleRustInput() const { return rustInputSnapshot(mappedInput); }' in (
    ROOT / 'src/activities/RustActivityAdapter.cpp').read_text()
assert '.input_snapshot = [](XpUiInput* out) { *out = rustInputSnapshot(*mapped); }' in (
    ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='reader-rejected-input-') as directory:
    work = Path(directory)
    (work / 'components').mkdir()
    (work / 'components/UiAppHelpers.h').write_text('#pragma once\n')
    (work / 'components/RustInputSnapshot.h').write_text(helper)
    def compile_fixture(name, fixture, archive=None):
        binary = work / name
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-pthread', '-fsanitize=address,undefined',
                        '-fno-sanitize-recover=all', '-I', str(work), '-I', str(HERE),
                        str(HERE / fixture), *rust_link_flags(archive=archive),
                        '-o', str(binary)], check=True)
        return binary
    subprocess.run([str(compile_fixture('snapshot', 'input_snapshot.cpp'))], check=True, timeout=30)
    swapped = helper.replace('mappedInput.wasPressed(buttons[i])', 'mappedInput.wasReleased(buttons[i])', 1)
    assert swapped != helper
    (work / 'components/RustInputSnapshot.h').write_text(swapped)
    failed = subprocess.run([str(compile_fixture('snapshot-order', 'input_snapshot.cpp'))],
                            capture_output=True, text=True, timeout=30)
    assert failed.returncode != 0 and 'physical snapshot probe order' in failed.stderr, failed.stderr
    print('600 physical sampler cases passed; probe-order mutant rejected', flush=True)
    subprocess.run([str(compile_fixture('owner', 'rejected_input_session.cpp'))], check=True, timeout=30)
    copied_repo = work / 'source'
    copied_rust = copied_repo / 'rust'
    shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
    for name in ('scripts', 'lib', 'src'):
        (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
    target = work / 'target'
    for name, before, after in [
        ('held-suppression-lost', 'gate.suppress_buttons &= input.held as u16;', 'gate.suppress_buttons = 0;'),
        ('failure-ignored', 'let failed = gate.failed;', 'let failed = false;'),
    ]:
        assert original.count(before) == 1
        changed = original.replace(before, after, 1)
        (copied_rust / path).write_text(changed)
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        failed = subprocess.run([str(compile_fixture(name, 'rejected_input_session.cpp',
                                                    str(target / 'release/libxp_ffi.a')))],
                                capture_output=True, text=True, timeout=30)
        assert failed.returncode != 0 and 'rejected input' in failed.stderr, failed.stderr
        print(json.dumps({'mutant': name, 'returncode': failed.returncode,
                          'source': str(path), 'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                          'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest()}), flush=True)
assert (ROOT / 'rust' / path).read_text() == original
assert helper_path.read_text() == helper
