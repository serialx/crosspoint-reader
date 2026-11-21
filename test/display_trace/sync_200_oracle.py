"""Apply reviewed upstream changes to the historical display trace fixtures."""

import hashlib
import json
import lzma
from pathlib import Path
import re

class Sync200Oracle:
    def __init__(self):
        self.directory = Path(__file__).resolve().parent / "sync-200-goldens"
        self.cases = json.loads((self.directory / "manifest.json").read_text())["cases"]

    def expected(self, name, historical):
        if name not in self.cases:
            return historical
        blocks = [block for block in re.split(rb"(?m)(?=^step )", historical) if block]
        for edit in self.cases[name]:
            index = edit["index"]
            block = blocks[index]
            lines = block.splitlines(keepends=True)
            if (lines[0].rstrip().decode() != edit["step"] or
                    hashlib.sha256(block).hexdigest() != edit["baseline_sha256"]):
                raise ValueError(f"{name}: historical trace changed at block {index}")
            start = edit["prefix_lines"]
            stop = len(lines) - edit["suffix_lines"]
            fragment = lzma.decompress((self.directory / edit["fragment"]).read_bytes())
            blocks[index] = b"".join([*lines[:start], fragment, *lines[stop:]])
        return b"".join(blocks)
