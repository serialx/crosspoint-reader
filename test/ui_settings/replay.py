"""Replay frozen C++ setting edits through the production Rust ABI."""
import gzip
import pathlib
import subprocess
import sys

source = pathlib.Path(__file__).with_name("edit.golden.gz")
subprocess.run([sys.argv[1]], input=gzip.decompress(source.read_bytes()), check=True)
