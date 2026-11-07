#!/usr/bin/env python3
"""Verify the immutable parent result file and its actual capture-source snapshot."""
import hashlib
import json
from pathlib import Path
p=Path(__file__).resolve().parent/'native_oracle'
d=json.loads((p/'provenance.json').read_text())
assert hashlib.sha256((p/d['oracle']).read_bytes()).hexdigest()==d['oracle_sha256']
snapshot=(p/d['capture_contract_snapshot']).read_bytes()
assert hashlib.sha256(snapshot).hexdigest()==d['source_sha256']['test/xtc_owner/Contract.cpp']
print(f'Parent outputs and {len(snapshot)} capture-source bytes unchanged; snapshot is not an include path')
