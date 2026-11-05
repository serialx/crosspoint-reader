"""Run the Rust cache owner through the unchanged frozen native read oracle."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from test_popup_resource import definition, native_leaf, replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    package = (ROOT / 'lib/Epub/Epub/PackagePlatform.cpp').read_text()
    template = (HERE / 'book_cache_fixture.h').read_text()
    text = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)',
        'std::string owned(XpAppText value)'))
    mutex = '\n'.join(definition(package, signature) for signature in (
        'void BookMetadataCache::lockReadCursor()', 'void BookMetadataCache::unlockReadCursor()'))
    source = replace_once(template, '// ACTUAL_TEXT_HELPERS', text)
    source = replace_once(source, '// ACTUAL_MUTEX_METHODS', mutex)
    fields = ('file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size', 'cache_lock', 'cache_unlock')
    leaves = '\n'.join('    value.' + name + ' = ' + native_leaf(native, name) + ';' for name in fields)
    source = replace_once(source, '// ACTUAL_LEAVES', leaves)
    fixture = source
    oracle = (ROOT / 'test/package_golden/PackageReadOracle.cpp').read_text()
    oracle = replace_once(oracle, '#include <HalStorage.h>', '')
    oracle = replace_once(oracle, '#include "Epub/BookMetadataCache.h"', '')
    source += '\n' + oracle
    configured = os.environ.get('XP_BOOK_CACHE_EVIDENCE_DIR')
    output = Path(configured) if configured else Path(tempfile.mkdtemp(prefix='book-cache-owner-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'
    cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-DXP_FFI_HOST_FAULT_INJECTION',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'),
               str(cpp), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(binary), str(output / 'files')], capture_output=True)
    (output / 'run.out').write_bytes(ran.stdout)
    (output / 'run.err').write_bytes(ran.stderr)
    if ran.returncode:
        raise RuntimeError(ran.stderr.decode())
    golden = ROOT / 'test/package_golden/read-golden.txt.gz'
    expected = gzip.decompress(golden.read_bytes())
    (output / 'results.json').write_text(json.dumps(dict(command=command,
        compile_returncode=built.returncode, run_returncode=ran.returncode,
        golden_sha256=hashlib.sha256(golden.read_bytes()).hexdigest(),
        actual_sha256=hashlib.sha256(ran.stdout).hexdigest(),
        expected_sha256=hashlib.sha256(expected).hexdigest(),
        exact=ran.stdout == expected), indent=2) + '\n')
    if ran.stdout != expected:
        for index, (actual, frozen) in enumerate(zip(ran.stdout.splitlines(), expected.splitlines())):
            if actual != frozen:
                raise RuntimeError(f'Frozen read oracle differs on line {index + 1}: {actual[:120]!r} vs {frozen[:120]!r}')
        raise RuntimeError('Frozen read oracle output length differs')
    print('Rust cache owner matches every frozen truncated and reused-object read outcome exactly.')
    query = output / 'queries.cpp'
    query.write_text(fixture + '\n' + (HERE / 'book_cache_queries.cpp').read_text())
    query_binary = output / 'queries'
    query_command = [str(query) if value == str(cpp) else str(query_binary) if value == str(binary) else value for value in command]
    built = subprocess.run(query_command, capture_output=True)
    (output / 'queries-compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(query_binary), str(output / 'query-files')], capture_output=True, timeout=60)
    (output / 'queries-run.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode:
        raise RuntimeError(ran.stderr.decode())
    (output / 'queries-results.json').write_text(json.dumps(dict(command=query_command,
        compile_returncode=built.returncode, run_returncode=ran.returncode), indent=2) + '\n')
    print('Retained-cursor rows, window interleaving, sink re-entry, OOM and I/O fallbacks pass.')


if __name__ == '__main__':
    main()
