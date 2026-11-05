"""Real-renderer utility widget pixels, frozen before replacing C++ views."""
import gzip
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent

def packed(directory):
    output = bytearray()
    for path in sorted(Path(directory).glob("*.bin")):
        output.extend(path.name.encode()+b"\0")
        output.extend(path.read_bytes())
    return bytes(output)

if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        subprocess.run([sys.argv[1], sys.argv[2], directory], check=True)
        actual = packed(directory)
        assert actual == gzip.decompress((HERE / "pixels.golden.gz").read_bytes()), "Utility widget pixels changed"
