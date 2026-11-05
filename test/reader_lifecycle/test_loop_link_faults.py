"""Fault the real loop hyperlink copy and require recovery to stop dispatch.

Unrelated actions retain the full-loop fixture's historical hooks. This helper
checks the real Rust allocation, borrowed metadata copy, release, and loop return.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
from test_loop_session import transformed_fixture, replace_once
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags

PRODUCTION = Path('crates/xp-ffi/src/app/epub/loop_owner.rs')
FAILURE = b'loop link allocation failure must stop before menu/back/build fallthrough'

SUPPORT = r'''
#include <cstddef>
#include <cstdlib>
#include <cstring>
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_allocation_attempts();
extern "C" size_t xp_test_live_allocations();
static void faultCheck(bool condition, const char* reason) {
  if (!condition) {
    std::fprintf(stderr, "loop-link-fault assertion: %s\n", reason);
    std::fflush(stderr);
    std::abort();
  }
}
struct LinkFaultCounts {
  unsigned href = 0, haptic = 0, update = 0, menu = 0, back = 0, build = 0, buttons = 0;
  unsigned turn = 0, skip = 0, overlay = 0, readerMenu = 0, link = 0, tap = 0, touchLong = 0;
  unsigned metadata = 0, navigate = 0;
} linkFault;
static bool failHref = false, metadataBorrowActive = false;
static const uint8_t* metadataBorrow = nullptr;
'''

MAIN = r'''
int main() {
  for (unsigned fail = 0; fail != 2; ++fail) {
    faultCheck(xp_test_live_allocations() == 0, "previous case leaked Rust allocations");
    xp_test_fail_allocation_after(SIZE_MAX);
    EpubReaderActivity reader;
    SETTINGS = {};
    SETTINGS.touchReaderControls = true;
    ReaderUtils::touch = {};
    ReaderUtils::prev = ReaderUtils::next = ReaderUtils::tilt = ReaderUtils::menu = false;
    reader.mappedInput.touch = true;
    reader.mappedInput.tap = true;
    reader.mappedInput.longTouch = false;
    reader.renderer.frame = false;
    linkMatch = true;
    inRead = false;
    busy = false;
    lockAvailable = true;
    clockMs = 2000;
    bleturner::heap = false;
    bleturner::note = 0;
    ESP = {};
    RECENT_BOOKS.removed = true;
    logIndex = 0;
    linkFault = {};
    failHref = fail != 0;
    metadataBorrowActive = false;
    metadataBorrow = nullptr;
    std::printf("link-fault case=%u\n", fail);
    size_t attempts = 0;
    {
      loop_session::Bridge bridge(reader);
      faultCheck(xp_test_live_allocations() == 1, "fixture setup must retain only the Rust owner");
      xp_test_fail_allocation_after(fail ? 0 : SIZE_MAX);
      reader.loop();
      attempts = xp_test_allocation_attempts();
      faultCheck(attempts == 1, "href copy must be the sole Rust allocation after injection");
      faultCheck(xp_test_live_allocations() == 1, "loop must release copied href and retain only owner");
      faultCheck(!metadataBorrowActive && linkFault.metadata == 1,
                 "exactly one synchronous native metadata borrow must complete");
      faultCheck(linkFault.link == 1 && linkFault.tap == 1 && linkFault.touchLong == 1,
                 "both cases must reach the actual tap/link path");
      const bool noFallthrough = !linkFault.menu && !linkFault.back && !linkFault.build && !linkFault.buttons &&
                                 !linkFault.turn && !linkFault.skip && !linkFault.overlay && !linkFault.readerMenu;
      if (fail) {
        faultCheck(noFallthrough, "loop link allocation failure must stop before menu/back/build fallthrough");
        faultCheck(linkFault.href == 0 && linkFault.haptic == 0 && linkFault.navigate == 0,
                   "allocation failure must not navigate or produce haptics");
        faultCheck(linkFault.update == 1, "allocation failure must request exactly one update");
      } else {
        faultCheck(noFallthrough, "successful link navigation must return before unrelated input dispatch");
        faultCheck(linkFault.href == 1 && linkFault.haptic == 1 && linkFault.navigate == 1,
                   "successful copy must navigate once and produce one haptic");
        faultCheck(linkFault.update == 0, "successful link navigation must not request update");
      }
      faultCheck(lockDepth == 0 && lockInstances == 0 && loop_session::modelDepth == 0,
                 "loop must release all locks after success or allocation failure");
      xp_test_fail_allocation_after(SIZE_MAX);
    }
    faultCheck(xp_test_live_allocations() == 0, "destroy must release the Rust owner");
    faultCheck(!reader.section, "bridge destruction must release the native Section");
    std::printf("link-fault result=%u attempts=%zu href=%u haptic=%u update=%u live=0\n",
                fail, attempts, linkFault.href, linkFault.haptic, linkFault.update);
  }
  std::puts("loop link allocation: 2 cases; borrowed copy, one allocation, release, and no fallthrough");
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def physical_fixture():
    source = transformed_fixture((HERE / 'loop_oracle.cpp').read_text()).split('int main()', 1)[0]
    event = definition(source, 'static void event(')
    counted = '''static void event(const char* name, int a = 0, int b = 0) {
  if (std::strcmp(name, "href") == 0) ++linkFault.href;
  else if (std::strcmp(name, "haptic") == 0) ++linkFault.haptic;
  else if (std::strcmp(name, "update") == 0) ++linkFault.update;
  else if (std::strcmp(name, "menu-gesture") == 0) ++linkFault.menu;
  else if (std::strcmp(name, "back") == 0) ++linkFault.back;
  else if (std::strcmp(name, "build-tick") == 0) ++linkFault.build;
  else if (std::strcmp(name, "buttons") == 0) ++linkFault.buttons;
  else if (std::strcmp(name, "turn") == 0) ++linkFault.turn;
  else if (std::strcmp(name, "skip") == 0) ++linkFault.skip;
  else if (std::strcmp(name, "overlay-open") == 0) ++linkFault.overlay;
  else if (std::strcmp(name, "reader-menu") == 0) ++linkFault.readerMenu;
  else if (std::strcmp(name, "link-hit") == 0) ++linkFault.link;
  else if (std::strcmp(name, "tap") == 0) ++linkFault.tap;
  else if (std::strcmp(name, "touch-long") == 0) ++linkFault.touchLong;
  std::printf("%u %s %d %d\\n", logIndex++, name, a, b);
}'''
    source = replace_once(source, event, SUPPORT + counted)
    metadata = definition(source, '  leaves.book.section.page.metadata_link = ')
    source = replace_once(source, metadata, '''  leaves.book.section.page.metadata_link = [](void* raw, uint16_t index, XpEpubLinkSink sink, void* destination) -> uint8_t {
    auto& value = context(raw); locked(); assert(index == 0 && value.link);
    faultCheck(!metadataBorrowActive && ++linkFault.metadata == 1, "metadata leaf must be called exactly once");
    faultCheck(xp_test_allocation_attempts() == 0, "unexpected Rust allocation before href metadata sink");
    faultCheck(xp_test_live_allocations() == 1, "only owner may be live before href copy");
    XpEpubLink row{};
    row.href = text(value.link->href);
    faultCheck(row.href.len == 6 && std::memcmp(row.href.data, "target", 6) == 0,
               "native metadata must borrow exact target bytes");
    metadataBorrow = row.href.data;
    metadataBorrowActive = true;
    const uint8_t accepted = sink(destination, &row);
    metadataBorrowActive = false;
    faultCheck(xp_test_allocation_attempts() == 1, "Rust metadata sink must attempt one href allocation");
    faultCheck(accepted == uint8_t(!failHref), "real Rust sink must report allocation success or failure");
    faultCheck(xp_test_live_allocations() == (failHref ? 1u : 2u),
               "sink must own exactly the copied href on success and none on failure");
    return accepted;
  }''')
    navigate = definition(source, '  seed.navigate = ')
    source = replace_once(source, navigate, '''  seed.navigate = [](void* raw, XpAppText href, uint8_t save) {
    unlocked();
    faultCheck(!metadataBorrowActive && metadataBorrow && ++linkFault.navigate == 1,
               "navigation must follow completion of the native metadata borrow");
    faultCheck(href.data && href.data != metadataBorrow && href.len == 6 && std::memcmp(href.data, "target", 6) == 0,
               "navigation must use the actual owned Rust copy of target");
    faultCheck(xp_test_allocation_attempts() == 1 && xp_test_live_allocations() == 2,
               "owned href must remain live through navigation");
    assert(save == 1); context(raw).owner.navigateToHref(string(href), save != 0);
  }''')
    return source + MAIN


def mutant(source):
    block = definition(source, 'match self.link_href_at_point(')
    changed, count = re.subn(r'Err\(\(\)\)\s*=>\s*\{\s*self\.request_update\(\);\s*return;\s*\}',
                             'Err(()) => {}', block)
    assert count == 1, 'expected the link-copy recovery branch exactly once'
    return replace_once(source, block, changed)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_LOOP_LINK_FAULT_EVIDENCE_DIR'))
    args = parser.parse_args()
    production = (ROOT / 'rust' / PRODUCTION).read_text()
    changed = mutant(production)
    fixture = physical_fixture()
    paths = [HERE / name for name in ('loop_oracle.cpp', 'loop_session_fixture.h', 'loop_session_rust.inc',
                                     'overlay_actions_session_fixture.h', 'test_loop_session.py',
                                     'test_loop_link_faults.py', 'loop.golden.gz', 'loop-release.golden.gz')]
    paths.extend(sorted((ROOT / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs')))
    paths.extend(ROOT / name for name in ('test/cmake/xp_ffi_host.cpp', 'rust/crates/xp-ffi/src/app/resources.rs',
                                         'rust/crates/xp-ffi/include/xp_epub_session.h'))
    hashes = {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in paths}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-loop-link-faults-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / 'reader_loop_link_faults' if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=True)
            cpp = work / 'faults.cpp'; cpp.write_text(fixture)
            flags = rust_link_flags()

            def run(name, archive=None):
                binary = work / name
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
                                '-I', str(HERE), '-I', str(ROOT / 'src'), str(cpp), *link, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result

            baseline = run('baseline')
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            assert baseline.stdout.endswith(b'loop link allocation: 2 cases; borrowed copy, one allocation, release, and no fallthrough\n')
            summary = {'test_name': 'reader_loop_link_faults', 'cases': 2,
                       'allocator': 'actual Rust heap_caps ABI with existing host fault/count wrapper',
                       'policy': 'historical LoopHooks for unrelated actions; real Rust href copy, release, and error return',
                       'checks': ['one owner allocation before injection', 'sole post-injection allocation inside metadata sink',
                                  'borrow ends before navigation through distinct owned pointer', 'owner-only after loop',
                                  'zero allocations after destroy', 'failure update once with no later dispatch'],
                       'limitations': ['SceneGate rejection is not exercised by this allocation helper'],
                       'source_sha256': hashes, 'fixture_sha256': digest(fixture.encode()),
                       'baseline_stdout_sha256': digest(baseline.stdout)}
            (evidence / 'baseline.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
            copied_repo = work / 'source'; copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            (copied_rust / PRODUCTION).write_text(changed)
            target = work / 'target'
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            rejected = run('link-copy-failure-falls-through', target / 'release/libxp_ffi.a')
            killed = rejected.returncode != 0 and FAILURE in rejected.stderr
            record = {'test_name': 'reader_loop_link_faults', 'mutant': 'link-copy-failure-falls-through',
                      'kind': 'production-rust', 'source': str(PRODUCTION),
                      'original_source_sha256': digest(production.encode()), 'mutant_source_sha256': digest(changed.encode()),
                      'returncode': rejected.returncode, 'specific_assertion': FAILURE.decode(),
                      'stdout_sha256': digest(rejected.stdout), 'stderr_sha256': digest(rejected.stderr), 'killed': killed}
            (evidence / 'mutant.json').write_text(json.dumps(record, indent=2) + '\n')
            print(json.dumps(record), flush=True)
            assert killed, rejected.stderr.decode(errors='replace')
    finally:
        for path, before in hashes.items():
            assert digest((ROOT / path).read_bytes()) == before, ('source changed during test', path)


if __name__ == '__main__':
    main()
