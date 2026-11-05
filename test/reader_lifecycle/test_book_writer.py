"""Compare production Rust cache files with independently encoded v10 bytes."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unicodedata
import zipfile
from test_popup_resource import definition, native_leaf, replace_once
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def string(value):
    return struct.pack('<I', len(value)) + value


def expected(count, raw_size=None):
    rows = []
    cumulative = 0
    previous = -1
    spine = bytearray()
    temporary = bytearray()
    toc = bytearray()
    offsets = []
    toc_offsets = []
    for i in range(count):
        href = (f'OPS//X/../ch{i:04d}.xhtml' if i % 2 else f'OPS/ch{i:04d}.xhtml').encode()
        offsets.append(len(spine))
        temporary += string(href) + struct.pack('<Ih', 0, -1)
        cumulative += raw_size if raw_size is not None else i % 31
        if i % 3 == 0:
            previous = len(toc_offsets)
            toc_offsets.append(len(toc))
            title = unicodedata.normalize('NFC', f'Cafe\u0301 {i}').encode()
            toc += string(title) + string(href) + string(f'a{i}'.encode()) + struct.pack('<Bh', i % 4, i)
        spine += string(href) + struct.pack('<Ih', cumulative, previous)
    fields = [b'A\xff\0Z', b'Author', b'ko', b'OPS/cover.png', b'OPS/ch0000.xhtml']
    lut = 29 + sum(map(len, fields))
    data_start = lut + 4 * (count + len(toc_offsets))
    header = struct.pack('<BIHH', 10, lut, count, len(toc_offsets)) + b''.join(map(string, fields))
    lookup = b''.join(struct.pack('<I', data_start + offset) for offset in offsets)
    lookup += b''.join(struct.pack('<I', data_start + len(spine) + offset) for offset in toc_offsets)
    return {'book.bin': header + lookup + spine + toc, 'spine.bin.tmp': bytes(temporary), 'toc.bin.tmp': bytes(toc)}


def main():
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    source = (HERE / 'book_writer_oracle.cpp').read_text()
    helpers = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', helpers)
    fields = ('file_open_read', 'file_open_write', 'file_read', 'file_write', 'file_close', 'file_seek', 'file_size', 'path_remove', 'cache_lock', 'cache_unlock')
    leaves = '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in fields)
    source = replace_once(source, '// ACTUAL_LEAVES', leaves)
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', '\n'.join('  value.' + name + ' = ' + native_leaf(app, name) + ';' for name in ('buffer_allocate', 'buffer_release')))
    output = Path(os.environ.get('XP_BOOK_WRITER_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-writer-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'), str(cpp), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    results = []
    for count in (0, 1, 399, 400, 2053):
        path = output / f'{count}.epub'
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_STORED) as archive:
            for i in range(count): archive.writestr(f'OPS/ch{i:04d}.xhtml', b'x' * (i % 31))
        cache = output / f'cache-{count}'
        ran = subprocess.run([str(binary), str(path), str(cache), str(count), 'bytes'], capture_output=True, timeout=120)
        (output / f'{count}.log').write_bytes(ran.stdout + ran.stderr)
        if ran.returncode: raise RuntimeError(ran.stderr.decode())
        hashes = {}
        for filename, frozen in expected(count).items():
            actual = (cache / filename).read_bytes()
            assert actual == frozen, f'{count}: {filename}'
            hashes[filename] = hashlib.sha256(actual).hexdigest()
        results.append(dict(count=count, hashes=hashes))
    for extension in ('.TXT', '.mD'):
        path = output / ('raw' + extension); path.write_bytes(b'raw text\xff' * 17)
        cache = output / ('raw-cache' + extension)
        ran = subprocess.run([str(binary), str(path), str(cache), '400', 'bytes'], capture_output=True, timeout=120)
        (output / ('raw' + extension + '.log')).write_bytes(ran.stdout + ran.stderr)
        if ran.returncode: raise RuntimeError(ran.stderr.decode())
        for filename, frozen in expected(400, path.stat().st_size).items(): assert (cache / filename).read_bytes() == frozen
    ran = subprocess.run([str(binary), str(output / '400.epub'), str(output / 'faults'), '400', 'faults'], capture_output=True, timeout=120)
    (output / 'faults.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode: raise RuntimeError(ran.stderr.decode())
    (output / 'results.json').write_text(json.dumps(dict(command=command, cases=results, faults_returncode=ran.returncode), indent=2) + '\n')
    print('Exact book v10 and temporary bytes pass at 0/1/399/400/2053 spines, TXT/Markdown, native/Rust OOM and short-write cleanup.')


if __name__ == '__main__': main()
