"""Check owner disposal against actual GfxRenderer backup methods and a lock mutant."""
from pathlib import Path
import hashlib
import json
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


def main():
    source_path = Path('crates/xp-ffi/src/app/epub/settings.rs')
    original = (ROOT / 'rust' / source_path).read_text()
    before = 'let access = self.lock_resources();\n            self.discard_overlay_page(&access);'
    after = 'let access = unsafe { self.borrowed_resources() };\n            self.discard_overlay_page(&access);'
    assert original.count(before) == 1
    renderer = (ROOT / 'lib/GfxRenderer/GfxRenderer.cpp').read_text()
    methods = '\n'.join(definition(renderer, signature) for signature in (
        'void GfxRenderer::freeBwBufferChunks()', 'bool GfxRenderer::storeBwBuffer()',
        'void GfxRenderer::restoreBwBuffer('))
    template = (HERE / 'overlay_backup_concurrency.cpp').read_text()
    assert template.count('// ACTUAL_BACKUP_METHODS') == 1
    fixture = template.replace('// ACTUAL_BACKUP_METHODS', methods)
    with tempfile.TemporaryDirectory(prefix='reader-backup-concurrency-') as directory:
        work = Path(directory)
        source = work / 'oracle.cpp'
        source.write_text(fixture)

        def compile_fixture(name, archive=None):
            binary = work / name
            subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-pthread',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-I', str(HERE), str(source), *rust_link_flags(archive=archive),
                            '-o', str(binary)], check=True)
            return binary

        subprocess.run([str(compile_fixture('baseline'))], check=True, timeout=30)
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        changed = original.replace(before, after, 1)
        (copied_rust / source_path).write_text(changed)
        target = work / 'target'
        subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                        '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                        '--features', 'app-test'], cwd=copied_rust, check=True)
        failed = subprocess.run([str(compile_fixture('unlocked-disposal', str(target / 'release/libxp_ffi.a')))],
                                capture_output=True, text=True, timeout=30)
        assert failed.returncode != 0 and 'renderDepth == 1 && modelDepth == 0' in failed.stderr, failed.stderr
        print(json.dumps({'mutant': 'unlocked-loop-disposal', 'returncode': failed.returncode,
                          'source': str(source_path), 'replacement_count': 1,
                          'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                          'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest(),
                          'renderer_methods_sha256': hashlib.sha256(methods.encode()).hexdigest()}), flush=True)
    assert (ROOT / 'rust' / source_path).read_text() == original


if __name__ == '__main__':
    main()
