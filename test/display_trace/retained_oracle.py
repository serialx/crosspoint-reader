#!/usr/bin/env python3
"""Compare retained-driver allocation, cancellation, coverage and diagnostic traces."""
import argparse
import json
import lzma
from pathlib import Path
import subprocess

from oracle import compare


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--cpp-dir", type=Path, required=True)
    parser.add_argument("--rust-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    for profile, allocations in (("sticky", 2), ("picco", 2), ("papermono", 8)):
        for failure in range(-1, allocations + 1):
            name = f"{profile}-fail-{failure}"
            traces = []
            for side, directory in (("cpp", args.cpp_dir), ("rust", args.rust_dir)):
                trace = subprocess.run([str(directory.resolve() / f"retained_trace_{profile}"), str(failure)],
                                       check=True, capture_output=True, timeout=60).stdout
                (args.out / f"{name}-{side}.trace.xz").write_bytes(lzma.compress(trace))
                traces.append(trace)
            passed = compare(*traces, name)
            rows.append({"case": name, "passed": passed, "bytes": [len(trace) for trace in traces]})
            print(f"{name}: {'PASS' if passed else 'FAIL'}", flush=True)
    (args.out / "results.json").write_text(json.dumps(rows, indent=2) + "\n")
    return int(not all(row["passed"] for row in rows))


if __name__ == "__main__":
    raise SystemExit(main())
