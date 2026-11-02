"""Exercise extracted native child leaves with real nullable object allocation."""
import hashlib
import json
from pathlib import Path
import shutil
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
static void* expectedExtractor;
static bool checkExtractor, imageSourceExists;
static unsigned extractorInstalls, extractorClears, imageExtractions, imageDecodes;
static void requireExtractor();
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  ++outerAttempts;
  return failOuter ? nullptr : ::operator new(size);
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def fixture(native):
    source = physical_fixture()
    image = (ROOT / 'lib/Epub/Epub/blocks/ImageBlock.cpp').read_text()
    source = replace_once(source, 'struct Epub {', '''struct Epub {
      bool extractItemToFile(const char* source, const char* destination) {
        assert(this == expectedExtractor && lockDepth == 1 && !modelDepth);
        assert(std::string(source) == "OPS/unseen.png" && std::string(destination) == "/cache/unseen.png");
        assert(!imageSourceExists); imageSourceExists = true; ++imageExtractions; return true;
      }
''')
    before = 'struct ImageBlock {\n  static void releaseRenderCache() { event("images"); }\n  static void setExtractor(void*, void*) { event("extractor"); }\n};'
    source = replace_once(source, before, '''struct ImageBlock {
      using ExtractFn = bool (*)(void*, const char*, const char*);
      static void* extractCtx;
      static ExtractFn extractFn;
      static void releaseRenderCache() { event("images"); }
      static void setExtractor(void*, ExtractFn);
    };
    void* ImageBlock::extractCtx = nullptr;
    ImageBlock::ExtractFn ImageBlock::extractFn = nullptr;
''' + definition(image, 'void ImageBlock::setExtractor(') + '''
    static void requireExtractor() {
      if (checkExtractor && (ImageBlock::extractCtx != expectedExtractor || !ImageBlock::extractFn)) {
        std::fprintf(stderr, "EXPECTED_ASSERTION: SYNC_EXTRACTOR_RESTORED\\n"); std::exit(42);
      }
    }
    struct RecoveryBook { Epub* pointer; Epub& value() { return *pointer; } };
    static RecoveryBook book(void* raw) { return {static_cast<Epub*>(raw)}; }
''')
    source = replace_once(source, 'void requestUpdate() { event("update"); }',
                           'void requestUpdate() { requireExtractor(); event("update"); }')
    old = definition(source, 'template <class T, class... A>\nstd::unique_ptr<T> makeUniqueNoThrow(')
    source = replace_once(source, old, '#include "' + str(MEMORY) + '"')
    source = replace_once(source, '[](void*) { assert(false && "historical child fixture cannot finish"); }',
                          '[](void*) { ++finishCount; }')
    cases = (HERE / 'child_native_faults.cpp').read_text()
    cases = replace_once(cases, '// ACTUAL_OWNED_TEXT', definition(native, 'std::string owned(XpAppText value)'))
    for leaf in ('selection', 'dictionary', 'footnote', 'clippings', 'sync'):
        cases = replace_once(cases, '/* ACTUAL_' + leaf.upper() + ' */', native_leaf(native, 'child_' + leaf))
    cases = replace_once(cases, '/* ACTUAL_INSTALL_EXTRACTOR */', native_leaf(native, 'book_install_extractor'))
    cases = replace_once(cases, '// IMAGE_IO_ENUM', definition(image, 'enum class ImageIo') + ';')
    begin, end = '    case ImageIo::HasSource:', '    case ImageIo::SourceSize:'
    cases = replace_once(cases, '// ACTUAL_EXTRACTION_IO', begin + image.split(begin, 1)[1].split(end, 1)[0])
    return ALLOCATOR + source + cases


def main():
    native = NATIVE.read_text()
    paths = [NATIVE, MEMORY, HERE / 'child_native_faults.cpp', HERE / 'child_session_rust.inc',
             HERE / 'child_parent_oracle.cpp', HERE / 'child_session_fixture.h',
             HERE / 'test_child_native_faults.py', HERE / 'test_child_metadata_faults.py',
             ROOT / 'lib/Epub/Epub/blocks/ImageBlock.cpp',
             ROOT / 'rust/crates/xp-ffi/src/app/epub/sync.rs']
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in paths}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-native-child-faults-') as directory:
            work = Path(directory)
            flags = rust_link_flags()

            def run(name, contents, archive=None):
                source = work / (name + '.cpp')
                source.write_text(fixture(contents))
                binary = work / name
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
                                '-DXP_STRIP_RENDER=0', '-I', str(HERE), str(source), *link, '-o', str(binary)], check=True)
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
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            sync = copied_rust / 'crates/xp-ffi/src/app/epub/sync.rs'
            changed = replace_once(sync.read_text(),
                '            unsafe { (self.leaves.book_install_extractor)(book.as_ptr()) };', '')
            sync.write_text(changed)
            target = work / 'target'
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            rejected = run('sync-extractor-not-restored', native, target / 'release/libxp_ffi.a')
            assert rejected.returncode == 42 and rejected.stderr == b'EXPECTED_ASSERTION: SYNC_EXTRACTOR_RESTORED\n'
            print(json.dumps(dict(test_name='reader_child_native_faults',
                sync_recovery='all three allocation failures retain extractor; third renders an uncached source through real Rust image policy',
                physical_limits='book extraction and image decoder are physical fakes; actual native install and extraction dispatch are extracted',
                mutant='sync-extractor-not-restored', returncode=rejected.returncode,
                mutant_source_sha256=digest(changed.encode()))), flush=True)
    finally:
        for path in paths:
            assert digest(path.read_bytes()) == hashes[str(path.relative_to(ROOT))]


if __name__ == '__main__':
    main()
