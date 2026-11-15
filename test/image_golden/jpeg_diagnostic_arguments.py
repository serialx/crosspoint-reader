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
    for mode in ["decode", "probe"]:
        subprocess.run([sys.argv[1], str(path), mode], check=True)
        old = subprocess.run([sys.argv[2], str(path), mode], capture_output=True)
        assert old.returncode != 0 and b"width == 0 && height == 0" in old.stderr, (mode, old)
        print(mode, "old guard fails independently; current guard passes")
