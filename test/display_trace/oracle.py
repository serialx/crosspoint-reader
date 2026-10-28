#!/usr/bin/env python3
"""Record/verify display traces or compare output from another driver runner."""
import argparse
import gzip
import hashlib
import itertools
import json
import lzma
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
PROFILES = {
    "c3": ["ssd1677", "uc8253", "uc8279-x3", "uc8179", "uc8279-02", "uc8279-03", "uc8279-67", "uc8279-68"],
    "x4pro": ["ssd1677", "uc8179", "uc8279-02", "uc8279-03", "uc8279-67", "uc8279-68"],
    "x4c": ["ssd1677", "uc8179", "uc8279-02", "uc8279-03", "uc8279-67", "uc8279-68"],
    "sticky": ["ssd1677", "ssd2677"],
    "metalio": ["ssd1677"],
    "papermono": ["ssd1683"],
    "a4": ["uc8279c"],
    "picco": ["yrd", "depg", "opm", "ssd2677", "unknown"],
}


def cases():
    yield "c3-bus-single", "c3", "bus", "single"
    for profile, controllers in PROFILES.items():
        for controller in controllers:
            for buffers in ("single", "dual"):
                yield f"{profile}-{controller}-{buffers}", profile, controller, buffers


def read(path):
    path = pathlib.Path(path)
    data = path.read_bytes()
    if path.suffix == ".gz":
        return gzip.decompress(data)
    return lzma.decompress(data) if path.suffix == ".xz" else data


def compare(expected, actual, name):
    if expected == actual:
        return True
    step = "before first step"
    for number, (left, right) in enumerate(itertools.zip_longest(expected.splitlines(), actual.splitlines()), 1):
        if left and left.startswith(b"step "):
            step = left.decode()
        if left != right:
            print(f"{name}: mismatch at line {number}, {step}", file=sys.stderr)
            print(f"  expected: {left!r}\n  actual:   {right!r}", file=sys.stderr)
            return False
    return False


def main():
    parser = argparse.ArgumentParser(__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("record", "verify"):
        sub = commands.add_parser(command)
        sub.add_argument("--bin-dir", type=pathlib.Path, required=True)
        sub.add_argument("--goldens", type=pathlib.Path, default=HERE / "goldens")
        sub.add_argument("--case", action="append", help="exact case name; repeat to select several")
        sub.add_argument("--script", type=pathlib.Path, default=HERE / "scenarios.txt")
        sub.add_argument("--page-dir", type=pathlib.Path, help="private widthxheight.raw files; keep these goldens outside the repo")
        sub.add_argument("--full", action="store_true", help="emit full hex payloads instead of FNV-1a-64")
    sub = commands.add_parser("compare")
    sub.add_argument("expected", type=pathlib.Path)
    sub.add_argument("actual", type=pathlib.Path)
    commands.add_parser("list")
    args = parser.parse_args()
    if args.command == "compare":
        return 0 if compare(read(args.expected), read(args.actual), str(args.actual)) else 1
    if args.command == "list":
        print("\n".join(case[0] for case in cases()))
        return 0
    selected = [case for case in cases() if not args.case or case[0] in args.case]
    if not selected or args.case and set(args.case) - {case[0] for case in selected}:
        parser.error("unknown case")
    if args.command == "record":
        args.goldens.mkdir(parents=True, exist_ok=True)
    temporary = tempfile.TemporaryDirectory(prefix="display-trace-pages-")
    page_dir = args.page_dir or pathlib.Path(temporary.name)
    if not args.page_dir:
        frame = gzip.decompress((HERE / "fixtures/page-800x480.raw.gz").read_bytes())
        for width, height in ((800, 480), (792, 528), (768, 552)):
            raw = b"".join(frame[y*100:y*100+width//8] if y < 480 else bytes([255])*(width//8) for y in range(height))
            (page_dir / f"{width}x{height}.raw").write_bytes(raw)
    manifest = {"format": 1, "script_sha256": hashlib.sha256(args.script.read_bytes()).hexdigest(), "cases": {}}
    failures = 0
    for name, profile, controller, buffers in selected:
        script = HERE / "bus-scenarios.txt" if controller == "bus" else args.script
        command = [str(args.bin_dir.resolve() / f"display_trace_{profile}_{buffers}"), controller, str(script.resolve()),
                   "--full" if args.full else "--compact"]
        if controller != "bus":
            geometry = "792x528" if controller in ("uc8253", "uc8279-x3") else "768x552" if profile == "a4" else "800x480"
            command.append(str((page_dir / f"{geometry}.raw").resolve()))
        first = subprocess.run(command, capture_output=True, check=True, timeout=60).stdout
        second = subprocess.run(command, capture_output=True, check=True, timeout=60).stdout
        if not compare(first, second, name + " determinism"):
            failures += 1
            continue
        path = args.goldens / f"{name}.trace.xz"
        if args.command == "record":
            packed = lzma.compress(first)
            if len(packed) > 1_000_000:
                raise ValueError(f"fixture exceeds 1 MB: {name} ({len(packed)} bytes)")
            path.write_bytes(packed)
        elif not compare(read(path), first, name):
            failures += 1
        manifest["cases"][name] = {"sha256": hashlib.sha256(first).hexdigest(), "bytes": len(first),
                                    "lines": first.count(b"\n"), "deterministic": True}
        print(f"{name}: {'recorded' if args.command == 'record' else 'checked'}, {len(first)} bytes", flush=True)
    if args.command == "record":
        (args.goldens / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
