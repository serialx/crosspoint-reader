"""Pause actual selection lock acquisition while another task invalidates Section."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from extract import definition
from test_child_metadata_faults import physical_fixture
from test_child_parent import replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

PRODUCTION = Path('crates/xp-ffi/src/app/epub/child.rs')
LOCK = r'''
static std::mutex resourceMutex, modelMutex, pauseMutex;
static std::condition_variable pauseCondition;
static int pauseAt;
static bool blocked, released;
static thread_local bool workerThread = false;
static thread_local int lockOrdinal = 0;
struct RenderLock {
  RenderLock() {
    assert(!modelDepth);
    if (workerThread && ++lockOrdinal == pauseAt) {
      std::unique_lock<std::mutex> guard(pauseMutex);
      blocked = true;
      pauseCondition.notify_all();
      assert(pauseCondition.wait_for(guard, std::chrono::seconds(5), [] { return released; }));
    }
    resourceMutex.lock();
    ++lockDepth;
    event("lock");
  }
  ~RenderLock() {
    event("unlock");
    --lockDepth;
    resourceMutex.unlock();
  }
};
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture():
    source = physical_fixture()
    source = replace_once(source, 'static int lockDepth = 0, modelDepth = 0;',
                          'static thread_local int lockDepth = 0, modelDepth = 0;')
    old_lock = definition(source, 'struct RenderLock {') + ';'
    source = replace_once(source, old_lock, LOCK)
    return ('#include <chrono>\n#include <condition_variable>\n#include <future>\n#include <mutex>\n'
            + source + (HERE / 'child_section_interleaving.cpp').read_text())


def main():
    production_file = ROOT / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    before = b'state.scene_gate.failed || state.current.spine != i32::from(capture.spine)'
    assert production.count(before) == 2
    mutants = [
        ('late-rejection-ignored', production.replace(before, b'state.current.spine != i32::from(capture.spine)', 1)),
        ('captured-spine-ignored', production.replace(before, b'state.scene_gate.failed', 1)),
    ]
    files = [HERE / name for name in ('child_parent_oracle.cpp', 'child_session_fixture.h',
             'child_session_rust.inc', 'overlay_actions_session_fixture.h', 'child_section_interleaving.cpp',
             'test_child_metadata_faults.py', 'test_child_section_interleaving.py')]
    files.append(ROOT / 'rust/crates/xp-ffi/src/app/epub/test_support/child.rs')
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in files}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-child-interleaving-') as directory:
            work = Path(directory)
            source = work / 'interleaving.cpp'
            source.write_text(fixture())
            flags = rust_link_flags()

            def run(name, archive=None):
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                binary = work / name
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-pthread',
                                '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                '-DXP_STRIP_RENDER=0', '-I', str(HERE), str(source), *link, '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('interleaving')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            target = work / 'target'
            for name, changed in mutants:
                (copied_rust / PRODUCTION).write_bytes(changed)
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                rejected = run(name, target / 'release/libxp_ffi.a')
                assert rejected.returncode != 0
                assert b'rejected selection must not publish a child' in rejected.stderr, rejected.stderr.decode(errors='replace')
                print(json.dumps(dict(test_name='reader_child_section_interleaving', blocked_cases=4,
                    confirmed_result_cases=1, fixture_sha256=hashes, candidate_source_sha256=digest(production),
                    baseline_stdout_sha256=digest(baseline.stdout), mutant=name, kind='production-rust',
                    mutant_source_sha256=digest(changed), mutant_returncode=rejected.returncode,
                    mutant_stderr_sha256=digest(rejected.stderr))), flush=True)
    finally:
        assert production_file.read_bytes() == production
        for path in files:
            assert digest(path.read_bytes()) == hashes[str(path.relative_to(ROOT))]


if __name__ == '__main__':
    main()
