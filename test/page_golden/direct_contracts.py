"""Run production direct-page contract checks in isolated scratch storage."""
import pathlib
import gzip
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="page-direct-contracts-") as directory:
    font = pathlib.Path(directory) / "fallback.cpfont"
    root = pathlib.Path(__file__).resolve().parents[2]
    font.write_bytes(gzip.decompress((root / "test/text_golden/fonts/NotoSansHebrew_14.cpfont.gz").read_bytes()))
    subprocess.run([str(pathlib.Path(sys.argv[1]).resolve()), str(font)], cwd=directory, check=True)
