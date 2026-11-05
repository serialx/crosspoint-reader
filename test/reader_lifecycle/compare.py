import gzip
import pathlib
import subprocess
import sys

actual = subprocess.check_output([sys.argv[1]])
expected = gzip.decompress(pathlib.Path(sys.argv[2]).read_bytes())
if actual != expected:
    lines_a, lines_e = actual.splitlines(), expected.splitlines()
    for i, (a, e) in enumerate(zip(lines_a, lines_e)):
        if a != e:
            raise SystemExit(f"reader trace differs at line {i + 1}:\nexpected {e!r}\nactual   {a!r}")
    raise SystemExit(f"reader trace length differs: {len(lines_e)} -> {len(lines_a)} lines")
print(f"reader lifecycle: {len(actual.splitlines())} trace lines identical")
