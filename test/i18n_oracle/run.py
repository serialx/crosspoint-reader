#!/usr/bin/env python3
"""Compare every public i18n result with the pre-port C++ byte oracle."""
import argparse
import gzip
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "test"))
from rust_host import rust_link_flags


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", action="store_true")
    args = parser.parse_args()
    for name, options in [("all", []), ("en-fr", ["--builtin-langs", "EN,FR"]),
                          ("stripped", ["--strip-unused"])]:
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            subprocess.run([sys.executable, str(ROOT / "scripts/gen_i18n.py"),
                            str(ROOT / "lib/I18n/translations"), str(out),
                            "--src-dirs", str(ROOT / "src"), str(ROOT / "lib"), *options],
                           check=True, stdout=subprocess.DEVNULL)
            # Quoted includes in I18n.h must resolve against this configuration.
            for file in ["I18n.cpp", "I18n.h"]:
                (out / file).write_bytes((ROOT / "lib/I18n" / file).read_bytes())
            flags = []
            if "xp_i18n" in (out / "I18n.cpp").read_text():
                os.environ["XP_I18N_BUILTIN_LANGS"] = "EN,FR" if name == "en-fr" else "all"
                os.environ["XP_I18N_STRIP_UNUSED"] = "1" if name == "stripped" else "0"
                flags = rust_link_flags()
            executable = out / "dump"
            subprocess.run(["c++", "-std=c++20", "-O2", "-I", str(out),
                            str(HERE / "dump.cpp"), str(out / "I18n.cpp"),
                            str(out / "I18nStrings.cpp"), *flags, "-o", str(executable)], check=True)
            actual = subprocess.check_output([str(executable)])
            golden = HERE / (name + ".bin.gz")
            if args.capture:
                golden.write_bytes(gzip.compress(actual, mtime=0))
            else:
                expected = gzip.decompress(golden.read_bytes())
                if actual != expected:
                    mismatch = next((i for i, pair in enumerate(zip(actual, expected))
                                     if pair[0] != pair[1]), min(len(actual), len(expected)))
                    raise AssertionError(f"{name}: first differing byte {mismatch}")
            print(f"{name}: {len(actual)} bytes identical")


if __name__ == "__main__":
    main()
