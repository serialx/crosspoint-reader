#!/usr/bin/env python3
"""Compare controller operations and retained allocations with the C++ oracle."""
import argparse
import hashlib
import json
import pathlib
import subprocess

from oracle import cases, compare


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--cpp-bin-dir", type=pathlib.Path, required=True)
    parser.add_argument("--rust-bin-dir", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--case", action="append")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    for name, profile, controller, buffers in cases():
        if not controller.startswith("uc8") or controller == "uc8253":
            continue
        if args.case and name not in args.case:
            continue
        faults = [-1, 0, -2]
        if controller == "uc8279c":
            faults += [1, 2, 3, 4, 5]
        for fault in faults:
            key = f"{name}-fail-after-{fault}"
            traces = []
            for directory in (args.cpp_bin_dir, args.rust_bin_dir):
                command = [str(directory.resolve() / f"display_trace_{profile}_{buffers}"),
                           controller, "--ultrachip", "--compact", str(fault)]
                traces.append(subprocess.run(command, capture_output=True, check=True, timeout=120).stdout)
            passed = compare(*traces, key)
            if not passed:
                for label, data in zip(("cpp", "rust"), traces):
                    (args.output / f"{key}-{label}.trace").write_bytes(data)
            allocations = [line.decode() for line in traces[0].splitlines() if line.startswith(b"alloc ")]
            results.append({"case": key, "passed": passed, "bytes": len(traces[0]),
                            "cpp_sha256": hashlib.sha256(traces[0]).hexdigest(),
                            "rust_sha256": hashlib.sha256(traces[1]).hexdigest(), "allocations": allocations})
            print(f"{key}: {'PASS' if passed else 'FAIL'}; {len(traces[0])} bytes", flush=True)
    (args.output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    return 0 if results and all(row["passed"] for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
