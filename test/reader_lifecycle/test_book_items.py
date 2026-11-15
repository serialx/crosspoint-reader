"""Compare complete item reads through native Epub methods and Rust book owners."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zipfile
from test_popup_resource import definition, native_leaf, replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    output = Path(os.environ.get('XP_BOOK_ITEMS_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-items-'))
    output.mkdir(parents=True, exist_ok=True)
    stubs = output / 'stubs'; stubs.mkdir(exist_ok=True)
    storage = (ROOT / 'test/book_io/epub-stubs/HalStorage.h').read_text()
    storage = replace_once(storage, 'size_t read(void* buffer, size_t count)', 'int read(void* buffer, size_t count)')
    (stubs / 'HalStorage.h').write_text(storage)
    from book_items_composition import compose
    source = compose((HERE / 'book_items_oracle.cpp').read_text())
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    includes = ['test/book_io/epub-stubs', 'lib/Epub', 'lib/ZipFile', 'lib/Txt', 'lib/FsHelpers', 'lib/Utf8',
                'lib/Memory', 'lib/hal', 'lib/miniz/src', 'test/reader_lifecycle', 'test/ui_runtime',
                'test/chapter_html_slim_parser/stubs']
    sources = ['lib/ZipFile/ZipPlatform.cpp', 'lib/FsHelpers/FsHelpers.cpp', 'lib/Utf8/Utf8.cpp',
               'lib/Txt/TxtToHtml.cpp', 'lib/miniz/src/InflateStream.cpp', 'lib/Memory/BuildScratch.cpp', 'lib/hal/HalMemory.cpp']
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-DENABLE_SERIAL_LOG', '-DLOG_LEVEL=2', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I', str(stubs), *[arg for path in includes for arg in ('-I', str(ROOT / path))],
               str(cpp), *[str(ROOT / path) for path in sources], *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    payload = bytes(range(256)) * 160
    for name, method in [('stored.epub', zipfile.ZIP_STORED), ('deflated.epub', zipfile.ZIP_DEFLATED)]:
        with zipfile.ZipFile(output / name, 'w', compression=method) as archive:
            for path, data in [('OPS/body.xhtml', payload), ('empty', b''), ('secret', b'ZIP ciphertext')]:
                archive.writestr(path, data)
    (output / 'text.MD').write_bytes(b'# title\n' + b'<&> hello\n' * 1024)
    cases = []
    for path in ('stored.epub', 'deflated.epub', 'text.MD', 'missing.epub'):
        for href in ('', 'OPS/body.xhtml', '/OPS/x/../body.xhtml', 'empty', 'secret', 'absent'):
            for kind in range(5):
                for reject in (0, 1):
                    cases.append((path, href, kind, reject, {}))
    for options in ({'BOOK_PROTECTED': '1'}, {'BOOK_PROTECTED': '1', 'BOOK_PLAIN_SIZE': '6'},
                    {'BOOK_PROTECTED': '1', 'BOOK_PLAIN_SIZE': '0'}, {'BOOK_PROTECTED': '1', 'BOOK_BUFFER_FAIL': '1'},
                    {'BOOK_PROTECTED': '1', 'BOOK_PLAIN_SIZE': '8'}, {'BOOK_PROTECTED': '1', 'BOOK_DECRYPT_FAIL': '1'},
                    {'BOOK_KEY_ERROR': '1'}):
        for kind in range(5):
            if 'BOOK_BUFFER_FAIL' in options and kind < 3:
                continue
            for reject in (0, 1):
                cases.append(('deflated.epub', '/secret', kind, reject, options))
    results = []
    for index, (path, href, kind, reject, options) in enumerate(cases):
        case = output / str(index); case.mkdir()
        steps = 0
        for fault in range(10000):
            captures = []
            for mode in ('native', 'rust'):
                ran = subprocess.run([str(binary), mode, path, href, str(kind), str(fault), str(reject)],
                    cwd=output, env={**os.environ, **options}, capture_output=True, timeout=30)
                (case / f'{mode}-{fault}.log').write_bytes(ran.stdout + ran.stderr)
                assert ran.returncode == 0, (index, mode, fault, ran.stderr.decode())
                captures.append(ran.stdout)
            assert captures[0] == captures[1], (index, path, href, kind, reject, options, fault)
            if fault == 0:
                steps = int(re.search(rb'steps (\d+)', captures[0])[1])
            if fault >= steps: break
        results.append(dict(path=path, href=href, kind=kind, reject=reject, options=options, physical_faults=steps))
    (output / 'results.json').write_text(json.dumps(dict(command=command, cases=results,
        generated_source_sha256=hashlib.sha256(source.encode()).hexdigest()), indent=2) + '\n')
    print(f'{len(results)} item cases and every reached physical fault: native/Rust trace, status, bytes and cleanup match.')


if __name__ == '__main__': main()
