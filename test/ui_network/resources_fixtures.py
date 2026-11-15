"""Validate frozen inputs and expose scan fixtures to the Rust host test."""
from pathlib import Path
import hashlib
import json

ORACLE = Path(__file__).resolve().parent / 'native_oracle'
manifest = json.loads((ORACLE / 'wifi_resources_manifest.json').read_text())
for name, expected in manifest['files'].items():
    assert hashlib.sha256((ORACLE / name).read_bytes()).hexdigest() == expected, name
cases = json.loads((ORACLE / 'wifi_sort_cases.json').read_text())
saved = set(cases['recorded']['saved'])
for index, (name, rssi, encrypted) in enumerate(cases['recorded']['rows']):
    print('row', name.encode().hex() or '-', rssi, int(encrypted), int(name in saved), index)
print('sizes', *cases['required_extension']['sizes'])
