"""Bind retired transfer cases to named frozen inputs; keep ClipSelection live."""
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'reader_children'))
from oracle_sources import native_text

out = Path(sys.argv[1])
text = '#pragma once\n#include "activities/reader/RustEpubPage.h"\n#include "' + str(HERE.parent / 'reader_children/selection_legacy_wire.h') + '"\n'
for name in ('EpubReaderFootnoteSelectActivity', 'DictionaryWordSelectActivity'):
    text += native_text('src/activities/reader/' + name + '.h') + '\n'
source = native_text('src/activities/reader/RustEpubPage.h')
text += '\nnamespace frozenSelectionTransfer {\nusing epubPage::facade;\nusing epubPage::adopt;\n'
text += source[source.index('inline void* footnote('):source.index('inline void* selection(')]
text += '\n}\n'
(out / 'FrozenSelectionTransfer.h').write_text(text)
