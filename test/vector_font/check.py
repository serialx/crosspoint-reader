import difflib
import gzip
import pathlib
import subprocess
import sys

result = subprocess.check_output(sys.argv[1:3], text=True)
golden = pathlib.Path(__file__).with_name("golden.txt.gz")
if len(sys.argv) > 3 and sys.argv[3] == "record":
    golden.write_bytes(gzip.compress(result.encode(), mtime=0))
else:
    expected = gzip.decompress(golden.read_bytes()).decode()
    if result != expected:
        print("".join(difflib.unified_diff(expected.splitlines(True), result.splitlines(True))))
        sys.exit(1)
    print("Vector font cache matches")
