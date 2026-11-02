"""Exercise extracted native child leaves with real nullable object allocation."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from test_child_metadata_faults import physical_fixture
from test_child_parent import replace_once
from test_popup_resource import definition, native_leaf

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

NATIVE = ROOT / 'src/activities/reader/RustEpubResources.cpp'
MEMORY = ROOT / 'lib/Memory/Memory.h'
ALLOCATOR = r'''
#include <new>
#include <cstddef>
static bool failOuter;
static int outerAttempts, finishCount;
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  ++outerAttempts;
  return failOuter ? nullptr : ::operator new(size);
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture(native):
    source = physical_fixture()
    old = definition(source, 'template <class T, class... A>\nstd::unique_ptr<T> makeUniqueNoThrow(')
    source = replace_once(source, old, '#include "' + str(MEMORY) + '"')
    source = replace_once(source, '[](void*) { assert(false && "historical child fixture cannot finish"); }',
                          '[](void*) { ++finishCount; }')
    cases = (HERE / 'child_native_faults.cpp').read_text()
    cases = replace_once(cases, '// ACTUAL_OWNED_TEXT', definition(native, 'std::string owned(XpAppText value)'))
    for leaf in ('selection', 'dictionary', 'footnote', 'clippings', 'sync'):
        cases = replace_once(cases, '/* ACTUAL_' + leaf.upper() + ' */', native_leaf(native, 'child_' + leaf))
    return ALLOCATOR + source + cases


def main():
    native = NATIVE.read_text()
    paths = [NATIVE, MEMORY, HERE / 'child_native_faults.cpp', HERE / 'child_session_rust.inc',
             HERE / 'child_parent_oracle.cpp', HERE / 'child_session_fixture.h',
             HERE / 'test_child_native_faults.py', HERE / 'test_child_metadata_faults.py']
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in paths}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-native-child-faults-') as directory:
            work = Path(directory)
            flags = rust_link_flags()

            def run(name, contents):
                source = work / (name + '.cpp')
                source.write_text(fixture(contents))
                binary = work / name
                subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
                                '-DXP_STRIP_RENDER=0', '-I', str(HERE), str(source), *flags, '-o', str(binary)], check=True)
                return subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            baseline = run('native-child', native)
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout.endswith(b'native child allocation: 12 outer cases; three Rust sync capture failures; no lost Pages or captures\n')
            for leaf in ('child_selection', 'child_dictionary', 'child_footnote'):
                original = native_leaf(native, leaf)
                before = ('for (auto& page : pages) page.release();' if leaf == 'child_selection'
                          else 'if (!child) page.release();')
                changed = replace_once(original, before, '(void)child;')
                altered = replace_once(native, original, changed)
                rejected = run(leaf + '-consumes-on-failure', altered)
                assert rejected.returncode != 0
                assert b'AddressSanitizer' in rejected.stderr and (
                    b'heap-use-after-free' in rejected.stderr or b'double-free' in rejected.stderr), rejected.stderr.decode(errors='replace')
                print(json.dumps(dict(test_name='reader_child_native_faults', outer_cases=12,
                    rust_sync_capture_faults=3, fixture_sha256=hashes, native_source_sha256=digest(native.encode()),
                    scope='actual extracted child leaves and Memory.h nothrow allocation; Rust ownership and sync capture paths',
                    limitations=['native child classes and Page are physical fakes; position conversion uses the historical marker',
                                 'retained native vector/string internal allocation safety is explicitly deferred O4/O5 debt'],
                    baseline_stdout_sha256=digest(baseline.stdout), mutant=leaf + '-consumes-on-failure',
                    kind='production-native', mutant_source_sha256=digest(altered.encode()),
                    mutant_returncode=rejected.returncode, mutant_stderr_sha256=digest(rejected.stderr))), flush=True)
    finally:
        for path in paths:
            assert digest(path.read_bytes()) == hashes[str(path.relative_to(ROOT))]


if __name__ == '__main__':
    main()
