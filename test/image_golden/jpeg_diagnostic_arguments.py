import pathlib
import subprocess
import sys
import tarfile
import tempfile

with tempfile.TemporaryDirectory() as temporary:
    path = pathlib.Path(temporary) / "valid.jpg"
    with tarfile.open(pathlib.Path(__file__).with_name("corpus.tar.gz")) as archive:
        entry = next(item for item in archive if item.name.endswith("gray-37x29.jpg"))
        path.write_bytes(archive.extractfile(entry).read())
    subprocess.run([sys.argv[1], str(path)], check=True)
