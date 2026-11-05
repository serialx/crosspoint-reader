#!/usr/bin/env python3
"""Capture original C++ metadata/spine fallback and lookup behavior."""
import argparse
import gzip
import io
import json
from pathlib import Path
import zipfile
from run import run
from residual_oracle import package
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--oracle', required=True, type=Path)
p.add_argument('--record', action='store_true')
a = p.parse_args()
rows = {'missing': b'', 'truncated': package('')[:200]}
for target in ['', 'a.xhtml', 'missing.xhtml', '../OPS/a.xhtml']:
    source = zipfile.ZipFile(io.BytesIO(package('<img src="../cover.png">')))
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as dest:
        for name in source.namelist():
            data = source.read(name)
            if name == 'OPS/book.opf':
                data = data.replace(b'</guide>', f'<reference type="text" href="{target}"/></guide>'.encode())
            dest.writestr(name, data)
    rows[target or 'empty'] = buffer.getvalue()
golden = Path(__file__).with_name('getters-golden.json.gz')
actual = {name: run(a.oracle.resolve(), data, options={'BOOK_RESIDUAL_GETTERS':'1'}) for name, data in rows.items()}
if a.record:
    with gzip.open(golden, 'wt') as f:
        json.dump(actual, f, sort_keys=True)
else:
    with gzip.open(golden, 'rt') as f:
        expected = json.load(f)
    for name in actual:
        assert actual[name] == expected[name], name
print(f'PASS {len(rows)} original C++ getter traces')
