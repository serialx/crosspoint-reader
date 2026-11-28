#!/usr/bin/env python3
"""Run real ImageBlock/decoder faults with the existing public image fixtures."""
import pathlib
import subprocess
import sys
import tarfile
import tempfile

with tempfile.TemporaryDirectory(prefix="asset-faults-") as directory:
    with tarfile.open(pathlib.Path(__file__).parents[1] / "image_golden/corpus.tar.gz") as corpus:
        for source, target in [("rgb-37x29-0-0.jpg", "source.jpg"), ("RGB-37x29.png", "source.png")]:
            pathlib.Path(directory, target).write_bytes(corpus.extractfile(source).read())
    subprocess.run([sys.argv[1], directory], check=True)
