"""Compare initialized native cache-read outcomes captured before the Package port."""
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    result = subprocess.run([sys.argv[1], directory], stdout=subprocess.PIPE, check=True)
expected = gzip.decompress(Path(__file__).with_name("read-golden.txt.gz").read_bytes())
if result.stdout != expected:
    raise SystemExit("Package read outcomes differ from the initialized native baseline")
print("Package initialized short-read outcomes match")
