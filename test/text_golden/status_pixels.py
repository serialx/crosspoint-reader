"""Real built-in and public SD-font pixel comparisons; optional Korean qualification font."""
import argparse
import gzip
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("binary", type=Path)
p.add_argument("--korean-font", type=Path)
a = p.parse_args()
subprocess.run([str(a.binary)], check=True)
with tempfile.TemporaryDirectory(prefix="reader-status-") as directory:
    for name in ("NotoSans", "NotoSansArabic", "NotoSansHebrew"):
        source = Path(__file__).parent / "fonts" / (name + "_14.cpfont.gz")
        font = Path(directory) / source.stem
        font.write_bytes(gzip.decompress(source.read_bytes()))
        subprocess.run([str(a.binary), str(font), "primary"], check=True)
if a.korean_font:
    subprocess.run([str(a.binary), str(a.korean_font)], check=True)
