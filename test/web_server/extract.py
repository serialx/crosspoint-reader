#!/usr/bin/env python3
"""Compile unchanged server methods against deterministic host transport/storage leaves."""

import argparse
import gzip
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]

def function(source, name):
    pattern = r"^[^\n;{}]*\b" + re.escape(name) + r"\([^{}]*?\{"
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise ValueError(f"function not found: {name}")
    # Every selected definition ends at column zero; nested blocks are indented.
    line_end = source.find("\n", match.end())
    if "}" in source[match.end():line_end]:
        return source[match.start():line_end]
    end = source.find("\n}", match.end())
    return source[match.start():end + 2]


def generate(source):
    header = (ROOT / "src/network/CrossPointWebServer.h").read_text()
    header = re.sub(r"^#(?:pragma|include).*\n", "", header, flags=re.MULTILINE)
    header = header.replace(" private:", " public:")
    pieces = ['#include "Host.h"', '#include "Endpoints.h"', header]
    helpers = (ROOT / "lib/FsHelpers/FsHelpers.cpp").read_text()
    pieces.append("namespace FsHelpers {\n" + "\n".join(function(helpers, name) for name in
                  ["normalisePath", "isSafePathComponent", "checkFileExtension"]) + "\n}")
    for include in re.findall(r'^#include "(html/[^"\n]+)"', source, re.MULTILINE):
        pieces.append('#include "src/network/' + include + '"')
    pieces.append(re.sub(r'^#include[^\n]*\n', '', source, flags=re.MULTILINE))
    pieces.append('#include "Cases.h"')
    return "\n\n".join(pieces) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--arduinojson", type=pathlib.Path, required=True)
    parser.add_argument("--source", type=pathlib.Path, default=ROOT / "src/network/CrossPointWebServer.cpp")
    parser.add_argument("--record", action="store_true")
    parser.add_argument("--mutation-check", action="store_true")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    args.build.mkdir(parents=True, exist_ok=True)
    generated = args.build / "server.cpp"
    source = args.source.read_text()

    def run(text):
        generated.write_text(generate(text))
        instrumentation = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else []
        subprocess.run(["c++", "-std=c++20", "-funsigned-char", "-O1", "-g", *instrumentation, "-I" + str(ROOT / "test/web_server/stubs"),
                        "-I" + str(ROOT / "test/web_server"), "-I" + str(ROOT),
                        "-I" + str(args.arduinojson), str(generated), "-o", str(args.build / "oracle")], check=True)
        return subprocess.check_output([str(args.build / "oracle")])

    actual = run(source)
    (args.build / "actual.jsonl").write_bytes(actual)
    golden = ROOT / "test/web_server/golden.jsonl.gz"
    if args.record:
        golden.write_bytes(gzip.compress(actual, mtime=0))
    expected = gzip.decompress(golden.read_bytes())
    if actual != expected:
        old = [json.loads(line) for line in expected.splitlines()]
        new = [json.loads(line) for line in actual.splitlines()]
        failures = [b["case"] for a, b in zip(old, new) if a != b]
        raise SystemExit(f"web oracle mismatch: {failures}; expected {len(old)} cases, got {len(new)}")
    print(f"web oracle: {len(actual.splitlines())} cases match byte for byte")
    if args.mutation_check:
        changed = source.replace('"Missing path"', '"Missing PATH"', 1)
        if changed == source or run(changed) == expected:
            raise SystemExit("mutation check did not reject changed download error")
        generated.write_text(generate(source))
        print("mutation check: rejected changed download response")


if __name__ == "__main__":
    main()
