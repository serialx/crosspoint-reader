"""Load the actual Rust book and call it through the shipping native facade."""
from pathlib import Path
import hashlib
import json
import signal
import struct
import subprocess
import tempfile
import zipfile

from book_facade_composition import compile_command, facade_source

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def make_fixture(work):
    def string(value):
        return struct.pack('<I', len(value)) + value
    metadata = b''.join(map(string, [b'Title', b'Author', b'en', b'', b'a.xhtml']))
    table = 9 + len(metadata)
    rows = [string(b'a.xhtml') + struct.pack('<Ih', 6, 0),
            string(b'b.xhtml') + struct.pack('<Ih', 12, 0),
            string(b'Chapter') + string(b'a.xhtml') + string(b'anchor') + struct.pack('<Bh', 1, 0)]
    offset = table + 12
    offsets = []
    for row in rows:
        offsets.append(offset)
        offset += len(row)
    cache = work / 'cache'
    cache.mkdir()
    (cache / 'book.bin').write_bytes(struct.pack('<BIHH', 10, table, 2, 1) + metadata +
                                   struct.pack('<III', *offsets) + b''.join(rows))
    (cache / 'source.css').write_text('p { margin-top: 2em; }\n')
    source = work / 'source.epub'
    with zipfile.ZipFile(source, 'w', compression=zipfile.ZIP_STORED) as archive:
        archive.writestr('a.xhtml', b'first!')
        archive.writestr('b.xhtml', b'image!')
    return source, cache


def main():
    source = facade_source(ROOT) + (HERE / 'book_facade_oracle.cpp').read_text()
    with tempfile.TemporaryDirectory(prefix='reader-book-facade-') as directory:
        work = Path(directory)
        cpp, binary = work / 'facade.cpp', work / 'facade'
        cpp.write_text(source)
        subprocess.run(compile_command(ROOT, work, cpp, binary), check=True)
        book, cache = make_fixture(work)
        destination = work / 'image.out'
        subprocess.run([str(binary), str(book), str(cache), str(destination)], check=True)
        assert destination.read_bytes() == b'image!'
        for getter in ('path', 'title', 'author'):
            result = subprocess.run([str(binary), str(book), str(cache), str(destination), getter],
                                    capture_output=True)
            assert result.returncode == -signal.SIGABRT, (getter, result.returncode, result.stderr)
            assert b'requireOwned' in result.stderr
    print(json.dumps(dict(source_sha256=hashlib.sha256(source.encode()).hexdigest(),
                          actual=['ReaderBook load and callbacks', 'Epub facade and getters',
                                  'no native Section CSS on borrowed facade', 'ImageBlock extractor binding'],
                          limits=['two simulated ticks; actual Section/parser composition remains separate',
                                  'host filesystem and protection fakes'])))


if __name__ == '__main__':
    main()
