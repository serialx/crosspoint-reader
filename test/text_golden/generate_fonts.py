"""Regenerate public fixtures with the repository converter (needs its requirements)."""
import gzip
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / 'lib/EpdFont/builtinFonts/source'
for family, intervals in [('NotoSans', 'reading'), ('NotoSansArabic', 'ascii,arabic'),
                          ('NotoSansHebrew', 'ascii,hebrew')]:
    command = [sys.executable, str(ROOT / 'lib/EpdFont/scripts/fontconvert_sdcard.py'),
               '--size', '14', '--intervals', intervals, '--name', family,
               '-o', str(HERE / 'fonts' / f'{family}_14.cpfont')]
    for style in ['Regular', 'Bold', 'Italic', 'BoldItalic']:
        path = SOURCE / family / f'{family}-{style}.ttf'
        if path.exists():
            command += ['--' + style.lower(), str(path)]
    subprocess.run(command, check=True)
    output = HERE / "fonts" / f"{family}_14.cpfont"
    output.with_suffix(".cpfont.gz").write_bytes(gzip.compress(output.read_bytes(), mtime=0))
    output.unlink()
