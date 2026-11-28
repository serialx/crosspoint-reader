#!/usr/bin/env python3
"""Exercise progress through actual ReaderBook ZIP streams and the session guard."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from test_book_load import build


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--archive', type=Path)
    args = parser.parse_args()
    if args.archive:
        os.environ['XP_TEST_RUST_ARCHIVE'] = str(args.archive.resolve())
    output = args.build.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binary, _, _ = build(output, main_source=(ROOT/'test/bookmark_bytes/progress_owner.cpp').read_text())
    files = {
        'META-INF/container.xml': '<container><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'book.opf': '<package><metadata><title>Progress composition</title></metadata><manifest><item id="one" href="one.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="one"/></spine></package>',
        'one.xhtml': '<html><body><p>hello world</p><p>second paragraph</p></body></html>',
    }
    path = output/'source.epub'
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    run = subprocess.run([str(binary), str(path), str(output/'cache')], capture_output=True)
    (output/'run.log').write_bytes(run.stdout + run.stderr)
    if run.returncode:
        raise RuntimeError((run.stdout + run.stderr).decode())
    print(run.stdout.decode().splitlines()[-1])


if __name__ == '__main__':
    main()
