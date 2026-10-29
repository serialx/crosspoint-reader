#!/usr/bin/env python3
"""Compile the reader's production failure diagnostics in the page host probe.

The reader lifecycle oracle checks Rust's choice and ordering of these commands.
This probe supplies each failure directly to verify the emitted native text.
"""
import re
import sys
from pathlib import Path

source = Path(sys.argv[1]).read_text()
if 'XP_UI_READER_BOOK_COMMAND_LOG_PAGE_LOAD_FAILED' in source:
    messages = []
    for name in ['PAGE_LOAD_FAILED', 'METADATA_FAILED']:
        match = re.search(r'case XP_UI_READER_BOOK_COMMAND_LOG_' + name + r':\s*\{\s*(LOG_ERR\([^;]+;)', source)
        assert match is not None
        messages.append(match[1])
    body = 'if (!p) {\n' + messages[0] + '\n} else {\n' + messages[1] + '\n}\n'
else:
    start = '    if (!p || !currentPageMetadata.assign(*p)) {\n'
    end = '      automaticPageTurnActive = false;'
    assert source.count(start) == 1
    body = source.split(start, 1)[1].split(end, 1)[0]
    assert 'LOG_ERR(' in body and 'return' not in body
Path(sys.argv[2]).write_text('#include <Logging.h>\nvoid readerFailureDiagnostic(bool p) {\n' + body + '}\n')
