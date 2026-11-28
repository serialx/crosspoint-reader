"""Exercise the production Rust ZIP/inflater owners using physical file/heap leaves."""
import hashlib
import json
import os
from pathlib import Path
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
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    source = (HERE / 'book_zip_oracle.cpp').read_text()
    helpers = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', helpers)
    oom = (HERE / 'book_inflate_log_fixture.h').read_text()
    oom = replace_once(oom, '// ACTUAL_INFLATE_OOM_HELPER', definition(
        (ROOT / 'lib/miniz/src/InflateStream.cpp').read_text(),
        'void InflateStream::logAllocationFailure(const char* allocation, size_t bytes)'))
    source = replace_once(source, '// ACTUAL_INFLATE_OOM', oom)
    zip_source = (ROOT / 'lib/ZipFile/ZipPlatform.cpp').read_text()
    source = replace_once(source, '// ACTUAL_EXTRACT_ENUM', definition(zip_source, 'enum class ExtractEvent : uint32_t') + ';')
    source = replace_once(source, '// ACTUAL_EXTRACT_DIAGNOSTIC', definition(zip_source,
        '[](void* opaque, uint32_t event, uint32_t a, uint32_t b, uint32_t, uint32_t)'))
    fields = ('file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size', 'cache_lock', 'cache_unlock', 'inflate_oom_log', 'log', 'log_enabled')
    leaves = '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in fields)
    leaves += '\n  value.scratch_claim = [](uint32_t min_len) { ++claims; return (' + native_leaf(native, 'scratch_claim') + ')(min_len); };'
    leaves += '\n  value.scratch_release = [](const uint8_t* bytes) { ++releases; (' + native_leaf(native, 'scratch_release') + ')(bytes); };'
    source = replace_once(source, '// ACTUAL_LEAVES', leaves)
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', '\n'.join('  value.' + name + ' = ' + native_leaf(app, name) + ';' for name in ('buffer_allocate', 'buffer_release')))
    output = Path(os.environ.get('XP_BOOK_ZIP_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-zip-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(HERE / 'scratch_stubs'), '-I', str(ROOT / 'lib/Memory'),
               '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'), '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'),
               str(cpp), str(ROOT / 'lib/Memory/BuildScratch.cpp'), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    data = bytes(range(256)) * 513 + b'<html>reading</html>' * 4096
    expected = output / 'expected.bin'; expected.write_bytes(data)
    results = []
    for name, method in [('stored', zipfile.ZIP_STORED), ('deflated', zipfile.ZIP_DEFLATED)]:
        path = output / (name + '.epub')
        with zipfile.ZipFile(path, 'w', compression=method) as archive:
            archive.writestr('OPS/empty.xhtml', b'')
            archive.writestr('OPS/body.xhtml', data)
        ran = subprocess.run([str(binary), str(path), str(expected)], capture_output=True)
        (output / (name + '.log')).write_bytes(ran.stdout + ran.stderr)
        if ran.returncode: raise RuntimeError(ran.stderr.decode())
        results.append(dict(name=name, returncode=ran.returncode, zip_sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    (output / 'results.json').write_text(json.dumps(dict(command=command, results=results, expected_sha256=hashlib.sha256(data).hexdigest()), indent=2) + '\n')
    print('Stored/deflated bytes, ring wraps, early stop, retained open, scratch/heap cleanup and failures pass.')
if __name__ == '__main__': main()
