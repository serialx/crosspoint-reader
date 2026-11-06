"""Read the named, hashed native inputs retired by O4a; never use them as headers."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FROZEN = Path(__file__).with_name('native_oracle')
SNAPSHOTS = {entry['source']: entry for entry in json.loads((FROZEN / 'manifest.json').read_text())['snapshots']}


def oracle_path(path):
    path = Path(path)
    name = str(path.relative_to(ROOT) if path.is_absolute() else path)
    entry = SNAPSHOTS.get(name)
    return FROZEN / entry['snapshot'] if entry else ROOT / path


def native_bytes(path):
    path = Path(path)
    name = str(path.relative_to(ROOT) if path.is_absolute() else path)
    entry = SNAPSHOTS.get(name)
    data = oracle_path(path).read_bytes()
    if entry is None:
        return data
    assert len(data) == entry['bytes'] and hashlib.sha256(data).hexdigest() == entry['sha256'], name
    return data


def native_text(path):
    return native_bytes(path).decode()
