"""Frozen pre-port cache behavior at the glyph and group caps."""
import difflib
import gzip
import pathlib
import re
import subprocess
import sys

actual = subprocess.check_output([sys.argv[1], 'boundaries'], text=True)
actual = re.sub(r'fontData=0x[0-9a-f]+', 'fontData=PTR', actual)
with gzip.open(pathlib.Path(__file__).with_name('boundaries.txt.gz'), 'rt') as f:
    expected = f.read()
if actual != expected:
    print(''.join(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True))))
    sys.exit(1)
print('Font cache boundaries: all 126 transcripts match original C++')
