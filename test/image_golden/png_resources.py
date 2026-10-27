"""Check every Rust allocation failure and BMP scratch loan through the public ABI."""
from pathlib import Path
import subprocess
import sys
import tempfile
from png_cases import png

with tempfile.TemporaryDirectory() as directory:
    for color, depth in ((0, 1), (0, 8), (0, 16), (2, 8), (2, 16), (3, 4), (3, 8), (4, 8), (6, 8), (6, 16)):
        path = Path(directory) / f'{color}-{depth}.png'
        path.write_bytes(png(color=color, depth=depth))
        subprocess.run([sys.argv[1], str(path)], check=True)
