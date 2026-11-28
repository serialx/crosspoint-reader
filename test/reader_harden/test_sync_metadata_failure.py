"""Failed outbound metadata must still attempt the true local progress save."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path[:0] = [str(HERE), str(ROOT / 'test')]
from rust_host import rust_link_flags
from test_child_metadata_faults import physical_fixture
from test_child_parent import replace_once
source = physical_fixture()
source = replace_once(source, 'static bool allocationOk', 'static int saves, savedSpine, savedPage, savedCount;\nstatic bool failTitle;\nstatic bool allocationOk')
source = replace_once(source, 'event("save-progress", spine, page);', '++saves; savedSpine = spine; savedPage = page; savedCount = count;\n    event("save-progress", spine, page);')
source = replace_once(source, 'const auto toc = static_cast<Epub*>(raw)->getTocItem(index);', 'if (failTitle) return 0;\n    const auto toc = static_cast<Epub*>(raw)->getTocItem(index);')
source += r"""
int main() {
  for (int fault : {0, 1}) {
    saves = 0; savedSpine = savedPage = savedCount = -1;
    toc = fault == 0 ? -2 : 1; failTitle = fault == 1;
    saveOk = false;
    EpubReaderActivity owner;
    ChildSessionBridge session(owner);
    const auto book = owner.epub.get();
    const auto section = owner.section.get();
    const auto spine = owner.currentSpineIndex, page = owner.section->currentPage;
    owner.launchKOReaderSync();
    session.observe();
    assert(saves == 1 && owner.pendingSyncSaveError);
    assert(!owner.child && !owner.callback && owner.epub.get() == book && owner.section.get() == section);
    saveOk = true;
    owner.launchKOReaderSync();
    session.observe();
    assert(saves == 2 && savedSpine == spine && savedPage == page && savedCount == 9);
    assert(!owner.child && !owner.callback && owner.epub.get() == book && owner.section.get() == section);
  }
  std::puts("PASS: spine-index and TOC-title failure save true progress; write failure keeps error UI; no outbound child or teardown");
}
"""
with tempfile.TemporaryDirectory(prefix='reader-harden-sync-') as directory:
    work = Path(directory); cpp = work / 'fixture.cpp'; cpp.write_text(source); binary = work / 'fixture'
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION', '-DXP_STRIP_RENDER=0',
                    '-I', str(HERE), str(cpp), *rust_link_flags(), '-o', str(binary)], check=True)
    trace = subprocess.check_output([str(binary)])
    print(trace.splitlines()[-1].decode())
