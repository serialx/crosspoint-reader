from pathlib import Path
import subprocess
import sys
actual = subprocess.check_output([sys.argv[1], 'rust'])
expected = (Path(__file__).resolve().parent/'modes_geometry.golden').read_bytes()
if actual != expected:
    import difflib
    sys.stderr.writelines(difflib.unified_diff(expected.decode().splitlines(True), actual.decode().splitlines(True)))
    raise SystemExit(1)
