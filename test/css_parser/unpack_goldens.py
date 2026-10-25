#!/usr/bin/env python3
"""Expand checked-in CSS fixtures into a host-test output directory."""
import gzip
from pathlib import Path
import shutil
import sys

source, destination = map(Path, sys.argv[1:])
for path in sorted(source.rglob('*')):
    if not path.is_file():
        continue
    relative = path.relative_to(source)
    target = destination / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    if path.suffix == '.gz':
        target.with_suffix('').write_bytes(gzip.decompress(path.read_bytes()))
    else:
        shutil.copyfile(path, target)
