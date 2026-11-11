"""Detect accidental edits to the independent oracle or public font fixtures."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[3]
manifest = json.loads(Path(__file__).with_name("oracle-manifest.json").read_text())
for relative, expected in manifest["sha256"].items():
    assert hashlib.sha256((root / relative).read_bytes()).hexdigest() == expected, relative
print(f"verified {len(manifest['sha256'])} frozen/public inputs from {manifest['base']}")
