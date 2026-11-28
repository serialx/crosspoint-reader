#!/usr/bin/env python3
"""Compare linked firmware sections and symbols without rebuilding or checking out code.

Pass the target binutils prefix, for example /path/to/riscv32-esp-elf-.
Input directories must contain firmware.elf and firmware.bin. Alias symbols are
counted once by address and size; inlined code belongs to its containing symbol.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess


def output(*command):
    return subprocess.check_output(command, text=True)


def normalize(name):
    return re.sub(r"::h[0-9a-f]{16}$", "", name)


def inspect(directory, prefix):
    elf = directory / "firmware.elf"
    binary = directory / "firmware.bin"
    sections = {}
    for line in output(prefix + "size", "-A", str(elf)).splitlines():
        fields = line.split()
        if len(fields) == 3 and fields[0].startswith("."):
            sections[fields[0]] = int(fields[1])
    aliases = {}
    for line in output(prefix + "nm", "-S", "-C", "--defined-only", str(elf)).splitlines():
        fields = line.split(None, 3)
        if len(fields) != 4 or fields[2] not in "tTwW":
            continue
        address, size = int(fields[0], 16), int(fields[1], 16)
        if size:
            aliases.setdefault((address, size), set()).add(normalize(fields[3]))
    symbols = Counter()
    rows = []
    for (address, size), names in sorted(aliases.items()):
        names = sorted(names)
        symbols[names[0]] += size
        rows.append(dict(address=address, bytes=size, names=names))
    return dict(
        directory=str(directory.resolve()),
        firmware_bytes=binary.stat().st_size,
        elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
        firmware_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        sections=sections,
        symbols=dict(symbols),
        aliases=rows,
    )


def compare(before, after):
    def deltas(left, right):
        return {
            key: dict(before=left.get(key, 0), after=right.get(key, 0),
                      delta=right.get(key, 0) - left.get(key, 0))
            for key in sorted(left.keys() | right.keys())
            if left.get(key, 0) != right.get(key, 0)
        }

    return dict(
        firmware_delta=after["firmware_bytes"] - before["firmware_bytes"],
        sections=deltas(before["sections"], after["sections"]),
        symbols=deltas(before["symbols"], after["symbols"]),
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--tool-prefix", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before = inspect(args.before, args.tool_prefix)
    after = inspect(args.after, args.tool_prefix)
    result = dict(before=before, after=after, delta=compare(before, after))
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"firmware.bin: {result['delta']['firmware_delta']:+d} bytes")
    for section in (".flash.text", ".flash.rodata", ".dram0.data", ".dram0.bss", ".eh_frame"):
        delta = after["sections"].get(section, 0) - before["sections"].get(section, 0)
        print(f"{section}: {delta:+d} bytes")


if __name__ == "__main__":
    main()
