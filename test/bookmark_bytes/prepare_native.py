"""Materialize verified bookmark oracle sources only in the host build directory."""
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent / 'native_oracle'

def prepare(output):
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for row in manifest['snapshots']:
        data = (HERE / row['snapshot']).read_bytes()
        assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], row['source']
        target = output / row['source']
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    return manifest
