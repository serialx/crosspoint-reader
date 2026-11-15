"""Materialize verified, frozen native store inputs in the host build directory."""
import hashlib
import json
import re
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent / 'native_oracle'


def prepare(output):
    manifest = json.loads((HERE / 'manifest.json').read_text())
    for row in manifest['files']:
        data = (HERE / (row['source'] + '.txt')).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row['sha256'], row['source']
        target = output / row['source']
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.read_bytes() != data:
            target.write_bytes(data)
    snapshots = {}
    for row in manifest['snapshots']:
        data = (HERE / row['snapshot']).read_bytes()
        assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], row['source']
        snapshots[row['source']] = data.decode()
    for suffix in ('h', 'cpp'):
        source = snapshots['src/clippings/ClippingsManager.' + suffix]
        source = source.replace('#include "ClippingsManager.h"', '#include "ClippingExportOracle.h"')
        source = re.sub(r'\bClippingsManager\b', 'ClippingExportOracle', source)
        (output / ('ClippingExportOracle.' + suffix)).write_text(source)
    return manifest


if __name__ == '__main__':
    prepare(Path(sys.argv[1]))
