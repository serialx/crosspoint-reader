#!/usr/bin/env python3
"""Run production reader strip fault cases with the pinned public SD font."""
import gzip
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='reader-strip-replay-') as directory:
    work = pathlib.Path(directory)
    font = work / 'input.cpfont'
    font.write_bytes(gzip.decompress((root / 'test/text_golden/fonts/NotoSans_14.cpfont.gz').read_bytes()))
    subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()), str(font)], cwd=work, check=True)
