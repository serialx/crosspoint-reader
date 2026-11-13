"""Failed progress must not suppress status painting, page rendering or saving.

reader_harden/unit2 exercises the real size-window HAL failure producing None.
This owner composition injects that checked result through the host-only legacy
adapter; ReaderStatusPixels checks the actual composer's omission pixels.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path[:0] = [str(HERE), str(ROOT / 'test')]
from rust_host import rust_link_flags
from test_reader_settings import generated_i18n_directory
from test_book_render import replace_once

source = (HERE / 'book_render_oracle.cpp').read_text().split('int main() {', 1)[0]
source = b'''static bool unknownProgress;
static unsigned statusPaints;
extern "C" void xp_epub_test_view_status(void*);
extern "C" unsigned char xp_epub_test_overlay_scene_ready(void*);
''' + source.encode()
source = replace_once(source, b'#include "book_render_session_rust.inc"',
                      (HERE / 'book_render_session_rust.inc').read_bytes())
source = replace_once(source, b'/* ACTUAL_SECTION_START_BUILD */',
                      (HERE / 'legacy_section_start_build.inc').read_bytes())
# The page-contents hook replaces the production painter, including its footer.
source = replace_once(source, b'    contentsCalled = true;',
                      b'    contentsCalled = true; xp_epub_test_view_status(active->rustOwner);')
source = replace_once(source, b'  UNUSED_LEAF(book.book_progress);', b'''
  table.book.book_progress = [](void* book, int32_t, float) -> float {
    stable(); assert(book == active->epub.get());
    return unknownProgress ? -1.0f : 0.425f;
  };''')
source = replace_once(source, b'  UNUSED_LEAF(physical.radio_status);', b'''
  table.physical.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    stable(); *out = {}; if (peer) *peer = {};
  };''')
source = replace_once(source, b'  UNUSED_LEAF(physical.paint_status);', b'''
  table.physical.paint_status = [](const XpEpubStatus* value) {
    stable(); ++statusPaints;
    assert(value->book_progress == (unknownProgress ? -1.0f : 42.5f));
    assert(value->page == 4 && value->pages == 10);
  };''')
source = replace_once(source, b'out->has_ttf = active->renderer.ttf;',
                      b'out->has_ttf = active->renderer.ttf; out->title_mode = 2;')
source += b'''
int main() {
  for (bool unknown : {false, true}) {
    unknownProgress = unknown;
    ticks = 0; Section::seed = {}; SdCardFont::notice = false;
    EpubReaderActivity owner;
    owner.currentSpineIndex = 1; owner.nextPageNumber = 3;
    Section::seed.cached = true; Section::seed.complete = true;
    Section::seed.count = Section::seed.finalCount = 10;
    const auto before = statusPaints;
    owner.renderBook(); owner.observeOwner();
    assert(statusPaints > before);
    assert(owner.nativeSection && owner.nativeSection->currentPage == 3);
    assert(owner.lastSavedSpineIndex == 1 && owner.lastSavedPage == 3 && owner.lastSavedPageCount == 10);
    { book_render_adapter::Entry entry(owner);
      assert(xp_epub_test_overlay_scene_ready(owner.rustOwner));
      unknownProgress = !unknown;
      xp_epub_test_view_status(owner.rustOwner);
      unknownProgress = unknown;
      xp_epub_test_view_status(owner.rustOwner);
      assert(statusPaints == before + 3); }
  }
  puts("PASS: unknown progress paints status, renders page 4, saves position and keeps the scene usable");
}
'''
with tempfile.TemporaryDirectory(prefix='reader-status-failure-') as tmp:
    work = Path(tmp); cpp = work / 'fixture.cpp'; cpp.write_bytes(source)
    binary = work / 'fixture'; flags = rust_link_flags()
    i18n = generated_i18n_directory(flags[-1])
    subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-Wno-format',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-DBOOK_RENDER_OWNER=1', '-I', str(HERE), '-I', str(i18n),
                    str(cpp), *flags, '-o', str(binary)], check=True)
    trace = subprocess.check_output([str(binary)])
    assert trace.count(b'page-load 3') == 2
    assert trace.count(b'save-spine 1') == 2
    print(trace.splitlines()[-1].decode())
