"""Generate explicitly named fixtures from verified historical source text."""
import hashlib
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent / 'native_oracle'


def prepare(output):
    manifest = json.loads((HERE / 'manifest.json').read_text())
    sources = {}
    for row in manifest['snapshots']:
        data = (HERE / row['snapshot']).read_bytes()
        assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], row['source']
        sources[row['source']] = data.decode()
    output.mkdir(parents=True, exist_ok=True)
    def fixture(text):
        return re.sub(r'\bBookmarkEntry\b', 'BookmarkOracleEntry',
                      re.sub(r'\bBookmarkFile\b', 'BookmarkOracleFile', text))
    declarations = sources['src/BookmarkEntry.h'] + '\n' + sources['src/util/BookmarkFile.h'].replace(
        '#include "../BookmarkEntry.h"', '')
    implementation = sources['src/util/BookmarkFile.cpp'].replace(
        '#include "BookmarkFile.h"', '#include "BookmarkOracleFixture.h"')
    for name, text in [('BookmarkOracleFixture.h', declarations), ('BookmarkOracleFixture.cpp', implementation)]:
        target = output / name
        data = fixture(text)
        if not target.exists() or target.read_text() != data:
            target.write_text(data)
    return manifest
