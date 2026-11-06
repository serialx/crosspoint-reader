"""Run the Library composition against the public synthetic SD font."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "font_diff"))
from make_fixture import generate

with tempfile.TemporaryDirectory(prefix="library-header-") as directory:
    font = Path(directory) / "public.cpfont"
    generate(font)
    subprocess.run([sys.argv[1], str(font)], check=True)
