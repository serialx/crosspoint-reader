#!/usr/bin/env python3
"""Validate and copy raw hypher tries for Rust include_bytes!.

Upstream: https://github.com/typst/hypher/tree/main/tries
Hypher code is MIT/Apache-2.0; patterns retain their individual licenses:
https://github.com/typst/hypher/tree/main/patterns
"""

import argparse
from pathlib import Path


def write_trie(source: Path, destination: Path) -> None:
    blob = source.read_bytes()
    if len(blob) < 5:
        raise ValueError(f"Trie too small: {source}")
    root = int.from_bytes(blob[:4], "big")
    if not 4 <= root < len(blob):
        raise ValueError(f"Invalid root offset {root}: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(blob)
    print(f"wrote {destination} ({len(blob)} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', action='append', required=True)
    parser.add_argument('--output', action='append', required=True)
    args = parser.parse_args()
    if len(args.input) != len(args.output):
        parser.error('input/output counts must match')
    for source, destination in zip(args.input, args.output):
        write_trie(Path(source), Path(destination))


if __name__ == '__main__':
    main()
