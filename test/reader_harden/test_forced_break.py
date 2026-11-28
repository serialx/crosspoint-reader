"""Exact CP0 workload: healthy forced TOC break yields three pages, never two after a failed row."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zipfile
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'test/reader_lifecycle'), str(ROOT / 'test')]
from book_section_composition import section_command, section_fixture, section_owner_source
from rust_host import rust_link_flags
with tempfile.TemporaryDirectory(prefix='reader-harden-forced-break-') as directory:
    work = Path(directory)
    source = section_owner_source(ROOT)
    source += (ROOT / 'test/reader_lifecycle/book_section_oracle.cpp').read_text().split('static std::mutex renderMutex;')[0]
    source += (ROOT / 'test/reader_harden/forced_break.cpp').read_text()
    cpp = work / 'fixture.cpp'; cpp.write_text(source); binary = work / 'fixture'
    archive = os.environ.get('XP_TEST_RUST_ARCHIVE')
    if archive is None:
        archive = rust_link_flags()[-1]
    subprocess.run(section_command(ROOT, work, cpp, binary, archive), check=True)
    for mode in ['control', 'M2', 'M2-oom']:
        folder = work / mode; folder.mkdir(); book, cache = section_fixture(folder)
        html = b'<html><body><p>Opening paragraph.</p><p id="anchor">Subchapter starts here.</p>' + b'<p>Some reading text follows the subchapter.</p>' * 8 + b'</body></html>'
        with zipfile.ZipFile(book, 'w') as z:
            z.writestr('a.xhtml', html); z.writestr('b.xhtml', b'image!')
        data = (cache / 'book.bin').read_bytes()
        lut = struct.unpack_from('<I', data, 1)[0]
        row = struct.unpack_from('<I', data, lut)[0]
        length = struct.unpack_from('<I', data, row)[0]
        target = row if mode == 'M2-oom' else row + 4 + length + 4
        subprocess.run([str(binary), str(book), str(cache), mode, str(target), str(len(html))], check=True)
        if mode == 'control':
            assert (cache / 'sections/0.bin').stat().st_size == 2132
print('PASS: exact CP0 healthy 3-page/2132-byte section; short row read and row allocation failure publish no section')
