"""Check the named native inputs; historical transcript hashes remain separate."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'native_oracle/manifest.json').read_text())
for item in manifest['files']:
    path = root / 'native_oracle' / item['file']
    data = path.read_bytes()
    assert len(data) == item['bytes'], path
    assert hashlib.sha256(data).hexdigest() == item['sha256'], path
    assert item['file'] in (root / item['consumer']).read_text(), item['consumer']
    assert path.suffix == '.txt'
assert manifest['retained_bytes'] == sum(item['bytes'] for item in manifest['files'])
print(f"{len(manifest['files'])} named inputs, {manifest['retained_bytes']} retained bytes; hashes and consumers match")
