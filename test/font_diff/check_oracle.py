#!/usr/bin/env python3
"""Compare the built-in C++ oracle stream with the pre-port capture."""
import pathlib
import subprocess
import sys
import difflib

expected = pathlib.Path(__file__).with_name('builtin-oracle.txt').read_text()
actual = subprocess.check_output([sys.argv[1]], text=True)
if actual != expected:
    sys.stderr.writelines(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True),
                                           fromfile='pre-port C++', tofile='candidate'))
    raise SystemExit(1)
