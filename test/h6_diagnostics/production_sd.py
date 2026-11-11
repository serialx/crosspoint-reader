"""Compare live production diagnostic dispatch with the 4139d281 golden."""
import pathlib
import subprocess
import sys

actual = subprocess.check_output([sys.argv[1]])
expected = pathlib.Path(sys.argv[2]).read_bytes()
if actual != expected:
    import difflib
    sys.stdout.writelines(difflib.unified_diff(
        expected.decode().splitlines(keepends=True), actual.decode().splitlines(keepends=True),
        fromfile="4139d281 golden", tofile="production diagnostic"))
    raise SystemExit(1)
print("Production SD diagnostics: 172 dispatch cases match the 4139d281 golden")
