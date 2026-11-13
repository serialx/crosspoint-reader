"""Strict HAL regressions against shipping Rust (app-test is deliberately off)."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf, replace_once

p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--archive', type=Path)
args = p.parse_args()
args.build.mkdir(parents=True, exist_ok=True)
archive = args.archive
if archive is None:
    target = args.build / 'rust'
    subprocess.run(['cargo', 'build', '--offline', '--locked', '--release', '--manifest-path',
                    str(ROOT / 'rust/Cargo.toml'), '-p', 'xp-ffi', '--features', 'chapter-test,sd-font-catalog'],
                   env=dict(os.environ, CARGO_TARGET_DIR=str(target), CARGO_BUILD_JOBS='2'), check=True)
    archive = target / 'release/libxp_ffi.a'
native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
package = (ROOT / 'lib/Epub/Epub/PackagePlatform.cpp').read_text()
source = (ROOT / 'test/reader_lifecycle/book_cache_fixture.h').read_text()
source = source.replace('#include "physical_defaults.h"', '''
#include <cstring>
#include <cstdlib>
[[noreturn]] static void unexpected() { std::abort(); }
template<class T> T poisoned() {
  T value; auto trap = &unexpected;
  static_assert(sizeof(T) % sizeof(trap) == 0);
  for (size_t i = 0; i < sizeof(T); i += sizeof(trap))
    std::memcpy(reinterpret_cast<char*>(&value) + i, &trap, sizeof(trap));
  return value;
}
static XpEpubLeaves unexpectedEpubLeaves() { return poisoned<XpEpubLeaves>(); }
static XpAppResources unexpectedAppResources() { return poisoned<XpAppResources>(); }
''')
source = replace_once(source, '// ACTUAL_TEXT_HELPERS', '\n'.join(definition(native, x) for x in (
    'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)',
    'std::string owned(XpAppText value)')))
source = replace_once(source, '// ACTUAL_MUTEX_METHODS', '\n'.join(definition(package, x) for x in (
    'void BookMetadataCache::lockReadCursor()', 'void BookMetadataCache::unlockReadCursor()')))
source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join(
    'value.' + x + ' = ' + native_leaf(native, x) + ';' for x in (
        'file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size', 'cache_lock', 'cache_unlock')))
cpp = args.build / 'unit1.cpp'
cpp.write_text(source + (HERE / 'unit1.cpp').read_text())
binary = args.build / 'unit1'
cmd = ['c++', '-std=c++20', '-O0', '-g', '-UNDEBUG', '-DXP_FFI_HOST_FAULT_INJECTION',
       '-DXP_SD_FONT_CATALOG',
       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
       *[f'-I{ROOT / x}' for x in ('lib/Memory', 'test/reader_lifecycle',
         'test/chapter_html_slim_parser/stubs', 'rust/crates/xp-ffi/include')],
       str(cpp), str(ROOT / 'test/cmake/xp_ffi_host.cpp'), str(archive), '-o', str(binary)]
build = subprocess.run(cmd, capture_output=True)
(args.build / 'compile.log').write_bytes(build.stdout + build.stderr)
if build.returncode:
    raise RuntimeError(build.stderr.decode())

def field(value):
    return struct.pack('<I', len(value)) + value

metadata = b''.join(field(b'') for _ in range(5))
lut = 9 + len(metadata)
spine = lut + 8
row = field(b'chapter.xhtml') + struct.pack('<Ih', 12, 0)
toc = spine + len(row)
data = (struct.pack('<BIHH', 10, lut, 1, 1) + metadata
        + struct.pack('<II', spine, toc) + row
        + field(b'Chapter') + field(b'chapter.xhtml') + field(b'anchor') + struct.pack('<Bh', 0, 0))
cases = [('valid', data, 0)]
cases += [('truncated', data[:n], n) for n in range(len(data))]
for at in (1, lut, lut + 4, spine, toc):
    damaged = bytearray(data)
    struct.pack_into('<I', damaged, at, 0xffffffff)
    cases.append(('damaged', damaged, at))
cases += [('read', data, n) for n in (0, 1, 5, 7, 9, lut, lut + 4, spine, spine + 4, toc)]
cases += [('cache-seek', data, 0), ('open', data, 0), ('text', data, 0), ('text-empty', data, 0), ('seek', data, 0)]
results = []
for number, (mode, content, offset) in enumerate(cases):
    folder = args.build / f'{number:03d}-{mode}'
    folder.mkdir(exist_ok=True)
    (folder / 'book.bin').write_bytes(content)
    if mode.startswith('text'):
        (folder / 'source.txt').write_text('new source content' if mode == 'text' else '')
        (folder / 'html').mkdir(exist_ok=True)
        (folder / 'html/0.html').write_text('<!-- TXT_CACHE_VERSION: 1 --><html><body></body></html>')
    if mode == 'seek':
        with zipfile.ZipFile(folder / 'source.epub', 'w') as z:
            z.writestr('chapter.xhtml', b'HELLO READERS')
        offset = 30 + len('chapter.xhtml')
    command = [str(binary), mode, str(folder), str(offset)]
    run = subprocess.run(command, capture_output=True)
    (folder / 'run.log').write_bytes(run.stdout + run.stderr)
    results.append(dict(mode=mode, command=command, returncode=run.returncode))
(args.build / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
failures = [r for r in results if r['returncode']]
print(f'{len(results) - len(failures)}/{len(results)} strict shipping-reader cases passed')
assert not failures, [(r['mode'], r['returncode']) for r in failures]
