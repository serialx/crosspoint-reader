"""Compile the production reader entry method with a narrow host activity shell."""
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
start = source.index('void EpubReaderActivity::openFootnoteSelect(')
end = source.index('\nvoid EpubReaderActivity::', start + 1)
pathlib.Path(sys.argv[2]).write_text(
    '#include "FootnoteReader.h"\n'
    '#include "activities/reader/EpubReaderFootnoteSelectActivity.h"\n'
    '#include "CrossPointSettings.h"\n' + source[start:end] + '\n')
