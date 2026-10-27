"""Check every Rust allocation failure and BMP scratch loan through the public ABI."""
from pathlib import Path
import subprocess
import sys
import tempfile
from png_cases import png, transition_cases

with tempfile.TemporaryDirectory() as directory:
    for color, depth in ((0, 1), (0, 8), (0, 16), (2, 8), (2, 16), (3, 4), (3, 8), (4, 8), (6, 8), (6, 16)):
        path = Path(directory) / f'{color}-{depth}.png'
        path.write_bytes(png(color=color, depth=depth))
        subprocess.run([sys.argv[1], str(path)], check=True, timeout=10)
    for name, data in transition_cases().items():
        if not name.startswith(('leading-', 'bytewise-')):
            continue
        path = Path(directory) / (name + '.png')
        path.write_bytes(data)
        subprocess.run([sys.argv[1], str(path)], check=True, timeout=10)
    # The production facade must release its BuildScratch claim on truncated input.
    for color in (2, 3):
        data = png(color=color)
        path = Path(directory) / 'truncated.png'
        for end in range(len(data)):
            path.write_bytes(data[:end])
            for mode in (4, 5):
                subprocess.run([sys.argv[2], str(path), str(mode), 'scratch'],
                               check=True, timeout=2, stdout=subprocess.DEVNULL)
