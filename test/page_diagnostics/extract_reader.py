#!/usr/bin/env python3
"""Compile the reader's production failure-log branch in the page host probe."""
import sys
from pathlib import Path
source=Path(sys.argv[1]).read_text()
start='    if (!p || !currentPageMetadata.assign(*p)) {\n'
end='      automaticPageTurnActive = false;'
assert source.count(start)==1
body=source.split(start,1)[1].split(end,1)[0]
assert 'LOG_ERR(' in body and 'return' not in body
Path(sys.argv[2]).write_text('#include <Logging.h>\nvoid readerFailureDiagnostic(bool p) {\n'+body+'}\n')
