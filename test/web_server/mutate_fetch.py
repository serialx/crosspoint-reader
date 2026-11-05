#!/usr/bin/env python3
"""Reject mutations inside Rust fetch policy using the frozen C++ transcripts."""

import argparse
import pathlib
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--arduinojson", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    workspace = args.build.resolve() / "rust"
    shutil.copytree(root / "rust", workspace, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("target"))
    for name in ["scripts", "lib", "src"]:
        link = workspace.parent / name
        if not link.exists():
            link.symlink_to(root / name, target_is_directory=True)
    source = workspace / "crates/xp-ffi/src/web/fetch.rs"
    original = source.read_text()
    host = next(line.split(": ", 1)[1] for line in
                subprocess.check_output(["rustc", "-vV"], text=True).splitlines()
                if line.startswith("host:"))
    target = args.build.resolve() / "target"
    for name, before, after in [
        ("segment", "t.written.wrapping_sub(t.offset) >= t.limit",
         "t.written.wrapping_sub(t.offset) > t.limit"),
        ("heartbeat", "now.wrapping_sub(self.heartbeat) < 5000",
         "now.wrapping_sub(self.heartbeat) <= 5000"),
    ]:
        if original.count(before) != 1:
            raise SystemExit(f"{name}: expected exactly one policy predicate")
        source.write_text(original.replace(before, after))
        subprocess.run(["cargo", "build", "--manifest-path", str(workspace / "Cargo.toml"),
                        "-p", "xp-ffi", "--release", "--locked", "--target", host,
                        "--target-dir", str(target)], check=True)
        result = subprocess.run([
            sys.executable, str(root / "test/web_server/extract.py"),
            "--build", str(args.build / name), "--arduinojson", str(args.arduinojson),
            "--archive", str(target / host / "release/libxp_ffi.a")],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.build / f"{name}.log").write_text(result.stdout)
        if result.returncode != 1 or "web oracle mismatch:" not in result.stdout:
            raise SystemExit(f"{name}: mutation did not produce a transcript mismatch")
        print(f"Rejected Rust {name} predicate mutation", flush=True)
    source.write_text(original)


if __name__ == "__main__":
    main()
