"""Unpack existing public fixtures outside the source tree and run the differential."""
import argparse
import gzip
from pathlib import Path
import subprocess
p = argparse.ArgumentParser()
p.add_argument('--binary', required=True)
p.add_argument('--work-dir', type=Path, required=True)
a = p.parse_args()
a.work_dir.mkdir(parents=True, exist_ok=True)
fonts = []
for source in sorted((Path(__file__).resolve().parents[1] / 'text_golden/fonts').glob('*.cpfont.gz')):
    dest = a.work_dir / source.stem
    dest.write_bytes(gzip.decompress(source.read_bytes()))
    fonts.append(str(dest))
subprocess.run([a.binary, *fonts], check=True)
