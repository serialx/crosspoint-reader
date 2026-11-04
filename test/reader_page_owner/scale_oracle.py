#!/usr/bin/env python3
"""Compare the shipping ImageScale log with the immutable C3/S3 libc oracle."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent
metadata = json.loads((root / 'scale-target.json').read_text())
data = gzip.decompress((root / 'scale-target.tsv.gz').read_bytes())
assert hashlib.sha256(data).hexdigest() == metadata['uncompressed_sha256']
assert len(data.splitlines()) == metadata['vectors']
with tempfile.TemporaryDirectory(prefix='image-scale-') as directory:
    path = Path(directory) / 'target.tsv'
    path.write_bytes(data)
    subprocess.run([sys.argv[1], '--scale', str(path)], check=True)
