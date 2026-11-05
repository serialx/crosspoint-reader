"""Keep physical selector instrumentation; copy a separately bound owner entry.

Arguments: main-owned FootnoteReaderOwner.cpp, output reader cpp, production
selector cpp, output selector cpp. The original selector assertions are retained.
"""
from pathlib import Path
import sys

entry_path, reader_out, selector_path, selector_out = map(Path, sys.argv[1:])
if entry_path.name == 'EpubReaderActivity.cpp':
    raise SystemExit('Pass the main-owned FootnoteReaderOwner.cpp; legacy parent extraction is retired.')
if not entry_path.is_file():
    raise SystemExit('Missing FootnoteReaderOwner.cpp; install the complete package-4 draft described in MIGRATION.md.')
entry = entry_path.read_text()
for retired in ('xp_ui_reader_child(', 'xp_ui_reader_loop_run(', 'xp_ui_reader_menu_parent('):
    assert retired not in entry, retired
reader_out.write_text(entry)

selector = selector_path.read_text()
selector = ('#include <xp_ui_reader_chrome.h>\n'
            '#define xp_ui_reader_footnote_view(...) (!renderer.failFootnoteBuild && xp_ui_reader_footnote_view(__VA_ARGS__))\n'
            + selector)
selector = selector.replace('#include "EpubReaderFootnoteSelectActivity.h"',
                            '#include "activities/reader/EpubReaderFootnoteSelectActivity.h"')
for accessor in ('footnote', 'link'):
    selector = selector.replace('page->' + accessor + '(',
                                '(RenderLock::requireHeld(), page)->' + accessor + '(')
selector = selector.replace('state.selected', 'checkedModelSelection(state.selected)')
selector_out.write_text(selector)
