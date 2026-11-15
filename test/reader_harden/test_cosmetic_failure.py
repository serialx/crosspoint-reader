"""A failed chapter label must not suppress toolbar or status painting."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'test/reader_lifecycle'
sys.path[:0] = [str(HERE), str(ROOT / 'test')]
from rust_host import rust_link_flags
from test_reader_settings import generated_i18n_directory
from test_overlay_view import replace_once
with tempfile.TemporaryDirectory(prefix='reader-harden-cosmetic-') as directory:
    work = Path(directory); flags = rust_link_flags(); i18n = generated_i18n_directory(flags[-1])
    for kind in ['overlay', 'status']:
        stem = 'overlay_view' if kind == 'overlay' else 'view_values'
        source = (HERE / (stem + '_oracle.cpp')).read_text().split('int main(', 1)[0]
        source = 'static int rowFault, faultPaints;\n' + replace_once(source,
            '#include "' + stem + '_session_rust.inc"', (HERE / (stem + '_session_rust.inc')).read_text())
        if kind == 'overlay':
            source = replace_once(source, 'return 0;\n  };\n  table.book.book_toc =', 'return rowFault == 1 ? -2 : 0;\n  };\n  table.book.book_toc =')
            source = replace_once(source, 'const auto item = active->epub->getTocItem(index);', 'if (rowFault == 2) return 0;\n    const auto item = active->epub->getTocItem(index);')
            source = replace_once(source, '++paints;', '++paints; ++faultPaints;\n    assert(model->chapter.len == 0 && model->page_info.len > 0);')
            source = replace_once(source, 'if (active->realValues && active->overlay == EpubReaderActivity::Overlay::Toolbar)', 'if (false)')
            source += r"""
extern "C" uint8_t xp_epub_test_overlay_scene_ready(void*);
int main() {
  for (rowFault = 1; rowFault <= 2; ++rowFault) {
    EpubReaderActivity fixture; fixture.realValues = true;
    fixture.overlay = EpubReaderActivity::Overlay::Toolbar;
    fixture.epub->fixedProgress = true;
    const int before = faultPaints;
    fixture.renderOverlay();
    assert(faultPaints == before + 1);
    { overlay_view_adapter::Entry entry(fixture); assert(xp_epub_test_overlay_scene_ready(fixture.rustOwner)); }
  }
  puts("PASS: toolbar paints without unreadable spine/TOC title and publishes a usable scene");
}
"""
        else:
            source = replace_once(source, 'return active->epub->getTocIndexForSpineIndex(spine);', 'return rowFault == 1 ? -2 : active->epub->getTocIndexForSpineIndex(spine);')
            source = replace_once(source, 'const auto item = active->epub->getTocItem(index);', 'if (rowFault == 2) return 0;\n    const auto item = active->epub->getTocItem(index);')
            source = replace_once(source, 'paintedTitle = nativeText(value->title);', '++faultPaints; assert(value->title.len == 0);\n    paintedTitle = nativeText(value->title);')
            source += r"""
int main() {
  for (rowFault = 1; rowFault <= 2; ++rowFault) {
    EpubReaderActivity fixture;
    SETTINGS.spec.titleMode = 1;
    const int before = faultPaints;
    fixture.renderStatusBar();
    assert(faultPaints == before + 1);
  }
  puts("PASS: status bar paints without unreadable spine/TOC title");
}
"""
        cpp = work / (kind + '.cpp'); cpp.write_text(source); binary = work / kind
        macro = 'OVERLAY_VIEW_OWNER' if kind == 'overlay' else 'VIEW_VALUES_OWNER'
        subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-D' + macro + '=1',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        '-I', str(HERE), '-I', str(i18n), str(cpp), *flags, '-o', str(binary)], check=True)
        trace = subprocess.check_output([str(binary)])
        print(trace.splitlines()[-1].decode())
