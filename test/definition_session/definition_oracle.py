"""Pinned native Definition references; never production headers or candidate code."""
import hashlib
import json
from pathlib import Path
FROZEN = Path(__file__).with_name('native_oracle')
SOURCES = {row['source']: row for row in json.loads((FROZEN / 'manifest.json').read_text())['sources']}
def native_text(source):
    row = SOURCES[source]
    data = (FROZEN / row['snapshot']).read_bytes()
    assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], source
    return data.decode()
