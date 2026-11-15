#!/usr/bin/env python3
"""Compare complete module buffers with frozen ricmoo/QRCode 0.0.1 outputs."""
import gzip
import json
from pathlib import Path
import subprocess
import sys

goldens = json.loads(gzip.decompress(Path(__file__).with_name("goldens.json.gz").read_bytes()))
for case in goldens["cases"]:
    result = subprocess.run([sys.argv[1], case["mode"], str(case["version"])],
                            input=bytes.fromhex(case["input"]), capture_output=True, check=True)
    assert result.stdout == bytes.fromhex(case["bitmap"]), case["name"]
print(f'{len(goldens["cases"])} QR module buffers match the frozen encoder')
