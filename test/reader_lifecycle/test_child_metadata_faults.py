"""Fault the real Rust packed metadata allocation and verify every release path."""
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
from test_child_parent import replace_once

PRODUCTION = Path('crates/xp-ffi/src/app/epub/child.rs')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def physical_fixture():
    source = (HERE / 'child_parent_oracle.cpp').read_text().split('int main() {', 1)[0]
    source = replace_once(source, '// CHILD_RESOURCES', '')
    source = replace_once(source, '// CHILD_METHODS', (HERE / 'child_session_rust.inc').read_text())
    source = replace_once(source, 'struct Page {', 'static int livePages = 0;\nstruct Page {')
    source = replace_once(source, 'explicit Page(int n) : number(n) { event("page-new", n); }',
                          'explicit Page(int n) : number(n) { ++livePages; event("page-new", n); }')
    source = replace_once(source, '~Page() { event("page-drop", number); }',
                          '~Page() { --livePages; event("page-drop", number); }')
    source = replace_once(source, 'struct Epub {', 'static size_t titleBytes = 0;\nstruct Epub {')
    source = replace_once(source, 'return "Book title with enough bytes for a real string allocation";',
                          'return std::string(titleBytes, \'T\');')
    return source


def main():
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    before = b'core::mem::take(&mut (*self.guarded.get()).selection_metadata)'
    changed = replace_once(production, before, b'SelectionMetadata::default()')
    fixture_paths = [HERE / name for name in ('child_parent_oracle.cpp', 'child_session_rust.inc',
                     'child_session_fixture.h', 'overlay_actions_session_fixture.h',
                     'child_metadata_faults.cpp', 'test_child_metadata_faults.py')]
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in fixture_paths}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-child-metadata-') as directory:
            work = Path(directory)
            source = work / 'faults.cpp'
            source.write_text(physical_fixture() + (HERE / 'child_metadata_faults.cpp').read_text())
            flags = rust_link_flags()

            def run(name, archive=None):
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                binary = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
                                '-DXP_CHAPTER_SINK_FAULT_INJECTION', '-DXP_STRIP_RENDER=0',
                                '-I', str(HERE), str(source), *link, '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('faults')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout.endswith(b'metadata allocation: 20 cases; one allocation; failure, cancel, result and teardown release\n')
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            (copied_rust / PRODUCTION).write_bytes(changed)
            target = work / 'target'
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            rejected = run('metadata-retained-after-result', target / 'release/libxp_ffi.a')
            assert rejected.returncode != 0
            assert b'result releases captured metadata' in rejected.stderr, rejected.stderr.decode(errors='replace')
            print(json.dumps(dict(test_name='reader_child_metadata_faults', cases=20,
                title_bytes=[16, 128, 8192, 65536], allocator='production heap_caps ABI with existing host failure/counting wrapper',
                paths=['metadata reserve failure', 'native child failure', 'cancel', 'clipping result', 'owner teardown'],
                policy_replacements='same historical method boundaries as reader_child_parent; metadata allocation/release and Page ownership are actual Rust',
                limitations=['native child allocation failure is a physical fake; complete native allocation campaign remains separate',
                             'oversized-title stress is not the bounded final campaign workload'],
                candidate_source_sha256=digest(production), fixture_sha256=hashes,
                baseline_stdout_sha256=digest(baseline.stdout), mutant='metadata-retained-after-result',
                mutant_kind='production-rust', mutant_source_sha256=digest(changed),
                mutant_returncode=rejected.returncode, mutant_stderr_sha256=digest(rejected.stderr))), flush=True)
    finally:
        assert production_file.read_bytes() == production
        for path in fixture_paths:
            assert digest(path.read_bytes()) == hashes[str(path.relative_to(ROOT))]


if __name__ == '__main__':
    main()
