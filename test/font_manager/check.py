"""Compare font registration, diagnostics, ownership and manager state."""
import pathlib
import re
import subprocess
import sys

result = subprocess.check_output([sys.argv[1]], text=True)
result = re.sub(r"fontData=0x[0-9a-f]+", "fontData=PTR", result)
golden = pathlib.Path(__file__).with_name("golden.txt")
if len(sys.argv) > 2 and sys.argv[2] == "record":
    golden.write_text(result)
else:
    import difflib
    expected = golden.read_text()
    if result != expected:
        print("".join(difflib.unified_diff(expected.splitlines(True), result.splitlines(True))))
        sys.exit(1)
    print("Font manager golden matches")
