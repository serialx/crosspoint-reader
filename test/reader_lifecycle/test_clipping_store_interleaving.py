"""Exercise actual ClippingStore rewrite versus the Rust highlight reader.

--evidence preserves generated sources, commands and outputs in a new directory;
it never overwrites a golden or repository file.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

PRODUCTION = Path('crates/xp-ffi/src/app/epub/child.rs')
NATIVE = Path('src/activities/reader/RustEpubResources.cpp')
MARKER = 'reader must remain blocked while actual ClippingStore rewrite is paused'
LIMITATIONS = [
    'Host mutex/condition-variable ordering evidence, not QEMU or ESP32-C3 lock-hold timing.',
    'Actual ClippingStore rewrite/rollback and Rust selection-result/draw methods; physical in-memory filesystem.',
    'No native internal allocation-failure safety claim; injected failure is a physical write failure only.',
    'Child fixture historical signature hook remains 77; seed signatures are obtained from the actual owner export.',
    'Empty physical Page exercises actual store enumeration/draw entry, not word geometry or framebuffer pixels.',
    'Native child creation/export and existing child-parent method seams remain physical/historical fakes.',
]

LOCK = r'''
static std::mutex resourceMutex, modelMutex, pauseMutex;
static std::condition_variable pauseCondition;
static bool pauseArmed = false, writePaused = false, resumeWriter = false;
static bool readerProbed = false, readerWasBlocked = false, resumeEarlyReader = false;
static bool failPausedWrite = false;
static int writerDepthAtPause = -1;
static thread_local bool readerThread = false;
struct RenderLock {
  RenderLock() {
    assert(!modelDepth && lockDepth == 0);
    if (readerThread) {
      const bool acquired = resourceMutex.try_lock();
      {
        std::lock_guard<std::mutex> guard(pauseMutex);
        readerWasBlocked = !acquired;
        readerProbed = true;
        pauseCondition.notify_all();
      }
      if (acquired) {
        // The deliberate mutant must fail the blocking predicate, not race files.
        resourceMutex.unlock();
        std::unique_lock<std::mutex> guard(pauseMutex);
        assert(pauseCondition.wait_for(guard, std::chrono::seconds(10), [] { return resumeEarlyReader; }));
      }
    }
    resourceMutex.lock();
    ++lockDepth;
    event("lock");
  }
  ~RenderLock() {
    assert(lockDepth == 1 && !modelDepth);
    event("unlock");
    --lockDepth;
    resourceMutex.unlock();
  }
};
'''

HARNESS = r'''
#include <ClippingStore.h>
#include "clipping_owner_fixture.h"

// These helpers and lambda expressions are extracted verbatim from the bridge.
/* ACTUAL_PATH_TEXT */
/* ACTUAL_BORROWED_TEXT */
static const auto actualInsert = /* ACTUAL_INSERT */;
static const auto actualCount = /* ACTUAL_COUNT */;
static const auto actualRow = /* ACTUAL_ROW */;
static unsigned insertCalls = 0, countCalls = 0, rowCalls = 0, pageInfoCalls = 0;
static uint8_t insertResult = 255;
static XpEpubClipInsert inserted{};
static std::string insertedText, insertedChapter;
static void* emptyPage = nullptr;

// Called by the copied HalStorage stub, before touching its temporary-file data.
void clippingStoreWritePause(const std::string& path) {
  std::unique_lock<std::mutex> guard(pauseMutex);
  if (!pauseArmed || path.size() < 4 || path.compare(path.size() - 4, 4, ".tmp") != 0) return;
  assert(!modelDepth);
  pauseArmed = false;
  writePaused = true;
  writerDepthAtPause = lockDepth;
  pauseCondition.notify_all();
  assert(pauseCondition.wait_for(guard, std::chrono::seconds(10), [] { return resumeWriter; }));
  if (failPausedWrite) fake::failWrite = 0;
}

static void installActualStore() {
  childRuntime.model_lock = [] {
    modelMutex.lock();
    assert(!modelDepth);
    ++modelDepth;
  };
  childRuntime.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
  };
  childLeaves.physical.clippings_insert = [](const XpEpubClipInsert* row) -> uint8_t {
    assert(!modelDepth && row);
    // Capture the supplied facts, without asserting away the target mutant.
    ++insertCalls;
    inserted = *row;
    insertedText = borrowed(row->text);
    insertedChapter = pathText(row->chapter);
    insertResult = actualInsert(row);
    return insertResult;
  };
  childLeaves.physical.clippings_count = []() -> uint32_t {
    assert(lockDepth == 1 && !modelDepth);
    ++countCalls;
    return actualCount();
  };
  childLeaves.physical.clippings_row = [](uint32_t index, XpEpubClip* out) {
    assert(lockDepth == 1 && !modelDepth && out);
    ++rowCalls;
    actualRow(index, out);
  };
  childLeaves.book.section.page.page_info = [](void* raw, XpEpubPageInfo* out) {
    assert(lockDepth == 1 && !modelDepth && raw == emptyPage);
    ++pageInfoCalls;
    *out = {};  // Physical Page has no elements; no highlight policy is substituted.
  };
  childLeaves.physical.log_enabled = [](uint8_t) -> uint8_t { assert(!modelDepth); return 0; };
}

struct StoredRow {
  XpEpubClip range;
  uint32_t textOffset;
  uint16_t textLength;
  std::string text;
};
static std::vector<StoredRow> snapshotStore() {
  assert(lockDepth == 1 && !modelDepth);
  std::vector<StoredRow> rows;
  for (uint32_t index = 0; index < CLIPPINGS.clippingCount(); ++index) {
    const auto* physical = CLIPPINGS.clippingAt(index);
    assert(physical);
    StoredRow row{};
    actualRow(index, &row.range);
    row.textOffset = physical->textOffset;
    row.textLength = physical->textLength;
    assert(CLIPPINGS.readClippingText(index, row.text));
    assert(row.textLength == row.text.size());
    rows.push_back(std::move(row));
  }
  return rows;
}
static bool sameRange(const XpEpubClip& a, const XpEpubClip& b) {
  return a.spine == b.spine && a.start_page == b.start_page && a.end_page == b.end_page &&
         a.page_count == b.page_count && a.start_word == b.start_word && a.end_word == b.end_word &&
         a.signature == b.signature && a.start_offset == b.start_offset && a.end_offset == b.end_offset;
}
static void assertRows(const std::vector<StoredRow>& rows, const std::vector<StoredRow>& original,
                       bool failed, const std::string& newText) {
  assert(rows.size() == original.size() + (failed ? 0 : 1));
  for (size_t i = 0; i < original.size(); ++i) {
    assert(sameRange(rows[i].range, original[i].range));
    assert(rows[i].text == original[i].text && rows[i].textLength == original[i].textLength);
    if (failed) assert(rows[i].textOffset == original[i].textOffset);
  }
  if (!failed) {
    assert(rows.back().text == newText && rows.back().textLength == newText.size());
    assert(sameRange(rows.back().range, inserted.range));
  }
  for (size_t i = 1; i < rows.size(); ++i)
    assert(rows[i].textOffset >= rows[i - 1].textOffset + rows[i - 1].textLength);
}

static void storeInterleaving(const char* name, unsigned initial, bool failed) {
  static_assert(CLIPPING_MAX_PER_BOOK == 256, "This structural case targets the C3 limit");
  assert(!activeChild && !lockDepth && !modelDepth && livePages == 0);
  fake::reset();
  allocationOk = exportOk = true;
  loadLimit = 99;
  loads = 0;
  hit = -1;
  titleBytes = 128;
  toc = 1;
  insertCalls = countCalls = rowCalls = pageInfoCalls = 0;
  insertResult = 255;
  EpubReaderActivity owner;
  ChildSessionBridge session(owner);
  installActualStore();
  uint32_t signature = 0;
  std::vector<StoredRow> original;
  {
    RenderLock guard;
    signature = xp_epub_test_clipping_signature(session.rust);
    assert(signature == 77);  // Existing child fixture seam, disclosed in evidence.
    assert(CLIPPINGS.loadForBook("/interleaving/book.epub", "Store title", "Store author", "epub"));
    for (unsigned i = 0; i < initial; ++i) {
      const std::string value = "seed-" + std::to_string(i) + std::string(17 + (i % 29), char('a' + i % 26));
      assert(CLIPPINGS.addClipping(2, 1, 1, 6, 1, 2, 2, "Chapter", 1, value, signature,
                                  100 + i * 10, 105 + i * 10) == ClippingStore::AddResult::Added);
    }
    original = snapshotStore();
  }
  owner.startClipSelection(-1, -1, ClipSelectionActivity::Mode::Clip);
  assert(owner.child && owner.callback && livePages == 3);
  auto callback = std::move(owner.callback);
  owner.child.reset();
  assert(livePages == 0);
  ActivityResult result;
  ClippingResult clipping;
  clipping.text = "committed selection: " + std::string(153, 'Z');
  clipping.startOffset = 12345;
  clipping.endOffset = 12567;
  clipping.startWordIndex = 3;
  clipping.endWordIndex = 8;
  clipping.wordCount = 6;
  result.data = clipping;
  {
    std::lock_guard<std::mutex> guard(pauseMutex);
    pauseArmed = true;
    writePaused = resumeWriter = readerProbed = readerWasBlocked = resumeEarlyReader = false;
    failPausedWrite = failed;
    writerDepthAtPause = -1;
  }
  auto writer = std::async(std::launch::async, [&] { callback(result); });
  {
    std::unique_lock<std::mutex> guard(pauseMutex);
    assert(pauseCondition.wait_for(guard, std::chrono::seconds(10), [] { return writePaused; }));
  }
  Page page(3);
  emptyPage = &page;
  auto reader = std::async(std::launch::async, [&] {
    readerThread = true;
    RenderLock guard;
    assert(lockDepth == 1 && !modelDepth);
    {
      // Also wait for result delivery to finish before inspecting native fixture
      // state, including any spurious try_lock failure in a deliberate mutant.
      std::unique_lock<std::mutex> done(pauseMutex);
      assert(pauseCondition.wait_for(done, std::chrono::seconds(10), [] { return resumeEarlyReader; }));
    }
    xp_epub_test_clipping_draw(session.rust, &page, 1, 0, 0);
    auto rows = snapshotStore();
    readerThread = false;
    return rows;
  });
  std::exception_ptr blockingFailure;
  {
    std::unique_lock<std::mutex> guard(pauseMutex);
    assert(pauseCondition.wait_for(guard, std::chrono::seconds(10), [] { return readerProbed; }));
    // Assert the predicate BEFORE releasing the writer, but defer propagation
    // until both tasks finish so the deliberate mutant has safe teardown.
    try {
      if (!readerWasBlocked || writerDepthAtPause != 1)
        throw std::runtime_error("reader must remain blocked while actual ClippingStore rewrite is paused");
    } catch (...) {
      blockingFailure = std::current_exception();
    }
    resumeWriter = true;
    pauseCondition.notify_all();
  }
  assert(writer.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
  writer.get();
  {
    std::lock_guard<std::mutex> guard(pauseMutex);
    resumeEarlyReader = true;
    pauseCondition.notify_all();
  }
  assert(reader.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
  const auto rows = reader.get();
  emptyPage = nullptr;
  // Defer every store check until both tasks have completed, including mutants.
  {
    RenderLock guard;
    if (!blockingFailure) {
      assert(writerDepthAtPause == 1 && insertCalls == 1);
      assert(insertResult == static_cast<uint8_t>(failed ? ClippingStore::AddResult::SaveFailed
                                                       : ClippingStore::AddResult::Added));
      assert(failed == fake::failureTriggered);
      assert(insertedText == clipping.text && !insertedChapter.empty());
      assert(inserted.range.signature == signature && inserted.range.spine == 2);
      assert(inserted.range.start_page == 2 && inserted.range.end_page == 3);
      assert(inserted.range.start_offset == clipping.startOffset && inserted.range.end_offset == clipping.endOffset);
      assert(countCalls > 0 && rowCalls == rows.size() && pageInfoCalls == 1);
      assertRows(rows, original, failed, clipping.text);
      for (const auto& item : fake::files)
        assert(item.first.size() < 4 || (item.first.substr(item.first.size() - 4) != ".tmp" &&
                                        item.first.substr(item.first.size() - 4) != ".bak"));
    }
    // Rollback leaves dirty=true; reload the committed file before unload can save it.
    assert(CLIPPINGS.loadForBook("/interleaving/book.epub", "Store title", "Store author", "epub"));
    if (!blockingFailure) {
      const auto persisted = snapshotStore();
      assertRows(persisted, original, failed, clipping.text);
      assert(persisted.size() == rows.size());
      for (size_t i = 0; i < rows.size(); ++i) assert(persisted[i].textOffset == rows[i].textOffset);
    }
    CLIPPINGS.unload();
  }
  if (blockingFailure) std::rethrow_exception(blockingFailure);
  std::printf("store-interleaving case=%s initial=%u final=%zu rollback=%d reader_blocked=1 writer_depth=%d signature=%u\n",
              name, initial, rows.size(), failed, writerDepthAtPause, signature);
}
int main() {
  try {
    storeInterleaving("success", 2, false);
    storeInterleaving("failed-write-rollback", 2, true);
    storeInterleaving("limit-255-to-256", 255, false);
    assert(!activeChild && !lockDepth && !modelDepth && livePages == 0);
    std::puts("actual clipping store: 3 synchronized cases; render blocked through commit or rollback");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "interleaving assertion: %s\n", error.what());
    return 2;
  }
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--evidence', type=Path, help='new directory for durable source/build/runtime evidence')
    args = parser.parse_args()
    root = args.repo.resolve()
    here = root / 'test/reader_lifecycle'
    sys.path.insert(0, str(here))
    sys.path.insert(0, str(root / 'test'))
    from extract import definition
    from test_child_metadata_faults import physical_fixture
    from test_child_parent import replace_once
    from test_popup_resource import native_leaf
    from rust_host import rust_link_flags

    native = (root / NATIVE).read_text()
    production_file = root / 'rust' / PRODUCTION
    production = production_file.read_bytes()
    before = b'''let added = {
                    let _access = self.lock_resources();
                    // A confirmed result survives scene failure; the store has one loop writer.
                    unsafe { (self.leaves.clippings_insert)(&row) }
                };'''
    after = b'''let added = {
                    // Deliberate mutant: insertion no longer owns ResourceAccess.
                    unsafe { (self.leaves.clippings_insert)(&row) }
                };'''
    assert production.count(before) == 1, 'clippings_insert ResourceAccess mutation must match once'
    changed = production.replace(before, after, 1)
    source = physical_fixture()
    source = replace_once(source, 'static int lockDepth = 0, modelDepth = 0;',
                          'static thread_local int lockDepth = 0, modelDepth = 0;')
    source = replace_once(source, definition(source, 'struct RenderLock {') + ';', LOCK)
    source = re.sub(r'\bClippingStore\b', 'HistoricalClippingStore', source)
    source = re.sub(r'\bCLIPPINGS\b', 'historicalClippings', source)
    harness = HARNESS
    for marker, signature in (
        ('/* ACTUAL_PATH_TEXT */', 'const char* pathText(XpAppText value)'),
        ('/* ACTUAL_BORROWED_TEXT */', 'const std::string& borrowed(XpAppNativeText value)'),
    ):
        harness = replace_once(harness, marker, definition(native, signature))
    leaf_hashes = {}
    for marker, name in (('INSERT', 'clippings_insert'), ('COUNT', 'clippings_count'), ('ROW', 'clippings_row')):
        actual = native_leaf(native, name)
        leaf_hashes[name] = digest(actual.encode())
        harness = replace_once(harness, '/* ACTUAL_' + marker + ' */', actual)
    source = ('#include <chrono>\n#include <condition_variable>\n#include <future>\n#include <mutex>\n'
              '#include <stdexcept>\n#include <exception>\n#include <HalStorage.h>\n'
              + source + harness)
    stub_original = (root / 'test/library_builder/stubs/HalStorage.h').read_text()
    stub = replace_once(stub_original, 'namespace fake {',
                        'void clippingStoreWritePause(const std::string& path);\n\nnamespace fake {')
    stub = replace_once(stub, '  size_t write(const uint8_t* data, size_t size) {',
                        '  size_t write(const uint8_t* data, size_t size) {\n    clippingStoreWritePause(path);')
    paths = [Path(__file__).resolve(), root / NATIVE, production_file,
             root / 'src/ClippingStore.cpp', root / 'src/ClippingStore.h',
             root / 'lib/Utf8/Utf8.cpp', root / 'lib/Serialization/Serialization.h',
             root / 'test/library_builder/stubs/HalStorage.h', root / 'test/library_builder/stubs/Memory.h',
             root / 'test/library_builder/stubs/Logging.h', root / 'test/clipping_store/stubs/esp_random.h']
    paths += [here / name for name in ('child_parent_oracle.cpp', 'child_session_fixture.h', 'child_session_rust.inc',
              'overlay_actions_session_fixture.h', 'clipping_owner_fixture.h', 'test_child_metadata_faults.py',
              'test_child_parent.py', 'test_popup_resource.py', 'extract.py')]
    paths += [root / 'rust/crates/xp-ffi/src/app/epub/test_support' / name for name in ('child.rs', 'clipping.rs')]
    paths += [root / 'rust/crates/xp-ffi/src/app/epub/clipping.rs', root / 'test/rust_host.py',
              root / 'test/cmake/xp_ffi_host.cpp']
    inputs = {str(path): digest(path.read_bytes()) for path in paths}
    evidence = args.evidence.resolve() if args.evidence else None
    if evidence:
        evidence.mkdir(parents=True, exist_ok=False)
    commands = []
    report = {'test_name': 'reader_clipping_store_interleaving', 'status': 'started',
              'limitations': LIMITATIONS, 'input_sha256': inputs, 'production_leaf_sha256': leaf_hashes,
              'candidate_source_sha256': digest(production), 'mutant_source_sha256': digest(changed),
              'mutant': 'clippings-insert-resource-omitted', 'replacement_count': 1}

    def save(name, data):
        if evidence:
            (evidence / name).write_bytes(data)

    def command(argv, label, cwd=None, required=True, timeout=300):
        commands.append({'label': label, 'argv': list(map(str, argv)), 'cwd': str(cwd or root)})
        result = subprocess.run(list(map(str, argv)), cwd=cwd or root, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=timeout)
        save(label + '.stdout', result.stdout)
        save(label + '.stderr', result.stderr)
        if required and result.returncode:
            raise AssertionError((label, result.returncode, result.stderr.decode(errors='replace')))
        return result

    try:
        with tempfile.TemporaryDirectory(prefix='reader-clipping-store-interleaving-') as directory:
            work = Path(directory)
            cpp = work / 'interleaving.cpp'
            cpp.write_text(source)
            stubs = work / 'stubs'
            stubs.mkdir()
            (stubs / 'HalStorage.h').write_text(stub)
            # Memory.h uses a quoted HalStorage include; copy it too so both TUs
            # use the identical instrumented filesystem definitions.
            for name in ('Memory.h', 'Logging.h'):
                shutil.copyfile(root / 'test/library_builder/stubs' / name, stubs / name)
            shutil.copyfile(root / 'test/clipping_store/stubs/esp_random.h', stubs / 'esp_random.h')
            save('interleaving.cpp', source.encode())
            save('HalStorage.h', stub.encode())
            save('Memory.h', (stubs / 'Memory.h').read_bytes())
            save('Logging.h', (stubs / 'Logging.h').read_bytes())
            save('esp_random.h', (stubs / 'esp_random.h').read_bytes())
            save('mutant-child.rs', changed)
            flags = rust_link_flags()
            report['baseline_archive'] = str(flags[-1])
            report['baseline_archive_sha256'] = digest(Path(flags[-1]).read_bytes())

            def compile_and_run(name, archive=None):
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                binary = work / name
                command(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-pthread',
                         '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_STRIP_RENDER=0',
                         '-I', stubs, '-I', here, '-I', root / 'src', '-I', root / 'lib/Serialization',
                         '-I', root / 'lib/Utf8', cpp, root / 'src/ClippingStore.cpp', root / 'lib/Utf8/Utf8.cpp',
                         *link, '-o', binary], name + '-compile')
                return command([binary], name + '-run', required=False)

            baseline = compile_and_run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            expected_summary = b'actual clipping store: 3 synchronized cases; render blocked through commit or rollback\n'
            assert baseline.stdout.endswith(expected_summary)
            assert baseline.stdout.count(b'store-interleaving case=') == 3
            report['baseline_stdout_sha256'] = digest(baseline.stdout)
            report['baseline_stderr_sha256'] = digest(baseline.stderr)
            report['cases'] = ['success 2->3', 'failed physical write rollback 2->2', 'C3 limit 255->256']
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(root / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(root / name, target_is_directory=True)
            assert (copied_rust / PRODUCTION).read_bytes() == production
            (copied_rust / PRODUCTION).write_bytes(changed)
            target = work / 'target'
            command(['cargo', 'build', '--manifest-path', copied_rust / 'Cargo.toml', '--target-dir', target,
                     '-p', 'xp-ffi', '--release', '--locked', '--features', 'app-test'],
                    'mutant-cargo', cwd=copied_rust, timeout=600)
            mutant = compile_and_run('mutant', target / 'release/libxp_ffi.a')
            assert mutant.returncode == 2, (mutant.returncode, mutant.stderr.decode(errors='replace'))
            assert ('interleaving assertion: ' + MARKER).encode() in mutant.stderr, mutant.stderr.decode(errors='replace')
            assert b'AddressSanitizer' not in mutant.stderr and b'runtime error:' not in mutant.stderr
            report.update(status='passed', mutant_returncode=mutant.returncode,
                          mutant_stdout_sha256=digest(mutant.stdout), mutant_stderr_sha256=digest(mutant.stderr),
                          generated_cpp_sha256=digest(source.encode()), copied_storage_sha256=digest(stub.encode()))
    except BaseException as error:
        report.update(status='failed', error=repr(error))
        raise
    finally:
        report['commands'] = commands
        immutable = all(digest(path.read_bytes()) == inputs[str(path)] for path in paths)
        report['inputs_unchanged'] = immutable
        save('manifest.json', (json.dumps(report, indent=2) + '\n').encode())
        assert immutable, 'production or input fixture changed while harness ran'
    print(json.dumps(report), flush=True)
    print('Limitations: ' + ' '.join(LIMITATIONS), flush=True)
    if not evidence:
        print('Use --evidence NEW_DIRECTORY to preserve generated sources and build/runtime logs.', flush=True)


if __name__ == '__main__':
    main()
