"""Use public SD fonts as well as built-ins for RTL strip/scene coverage."""
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile

subprocess.run([sys.argv[1]], check=True)
with tempfile.TemporaryDirectory(prefix="xp-ui-") as directory:
    for name in ("NotoSansArabic_14", "NotoSansHebrew_14"):
        source = Path(__file__).parent / "fonts" / (name + ".cpfont.gz")
        font = Path(directory) / (name + ".cpfont")
        font.write_bytes(gzip.decompress(source.read_bytes()))
        subprocess.run([sys.argv[1], str(font)], check=True)
