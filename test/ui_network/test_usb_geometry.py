"""Compare Rust body geometry with the frozen SDK recording; no pixel claims."""
from pathlib import Path
import subprocess
import sys
actual = subprocess.check_output([sys.argv[1], 'rust'])
expected = (Path(__file__).resolve().parent/'usb_geometry.golden').read_bytes()
if actual != expected:
    import difflib
    sys.stderr.writelines(difflib.unified_diff(expected.decode().splitlines(True), actual.decode().splitlines(True)))
    raise SystemExit(1)
