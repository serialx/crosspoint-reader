"""Compose Rust decoder storage and the real native framebuffer scratch registry."""
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
    source = (HERE / 'book_scratch_oracle.cpp').read_text()
    helpers = '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)',
        'std::string owned(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', helpers)
    oom = (HERE / 'book_inflate_log_fixture.h').read_text()
    oom = replace_once(oom, '// ACTUAL_INFLATE_OOM_HELPER', definition(
        (ROOT / 'lib/miniz/src/InflateStream.cpp').read_text(),
        'void InflateStream::logAllocationFailure(const char* allocation, size_t bytes)'))
    source = replace_once(source, '// ACTUAL_INFLATE_OOM', oom)
    fields = ('file_open_read', 'file_read', 'file_close', 'file_seek', 'cache_lock', 'cache_unlock', 'inflate_oom_log')
    leaves = '\n'.join('  value.' + name + ' = ' + native_leaf(native, name) + ';' for name in fields)
    leaves += '\n  value.scratch_claim = [](uint32_t min_len) { ++claims; return (' + native_leaf(native, 'scratch_claim') + ')(min_len); };'
    leaves += '\n  value.scratch_release = [](const uint8_t* bytes) { ++releases; (' + native_leaf(native, 'scratch_release') + ')(bytes); };'
    source = replace_once(source, '// ACTUAL_LEAVES', leaves)
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    allocators = '\n'.join('  value.' + name + ' = ' + native_leaf(app, name) + ';' for name in ('buffer_allocate', 'buffer_release'))
    source = replace_once(source, '// ACTUAL_ALLOCATOR_LEAVES', allocators)
    output = Path(os.environ.get('XP_BOOK_SCRATCH_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-scratch-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(HERE / 'scratch_stubs'), '-I', str(ROOT / 'lib/Memory'),
               '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'), str(cpp),
               str(ROOT / 'lib/Memory/BuildScratch.cpp'), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    results = []
    for name, arguments in [('production', []), ('outlived-lender', ['reclaim'])]:
        ran = subprocess.run([str(binary), *arguments], capture_output=True)
        (output / (name + '.log')).write_bytes(ran.stdout + ran.stderr)
        if name == 'production':
            assert ran.returncode == 0, ran.stderr.decode()
        else:
            assert ran.returncode != 0 and b'scratch lease outlived framebuffer lender' in ran.stderr
        results.append(dict(name=name, returncode=ran.returncode))
    (output / 'results.json').write_text(json.dumps(dict(command=command, results=results), indent=2) + '\n')
    print('Real registry: claims, nested exclusion, initialized/aligned backing, fallbacks and exactly-once release pass; lender assertion rejects early reclaim.')
if __name__ == '__main__': main()
