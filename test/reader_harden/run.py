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
p.add_argument('--unit', choices=['1', '2', '3', 'all'], default='all')
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
// The shared archive includes document-test exports; this fixture must never call them.
extern "C" void cssTestFailAllocation(size_t) { unexpected(); }
extern "C" size_t cssTestAllocationAttempts() { unexpected(); }
''')
source = replace_once(source, '// ACTUAL_TEXT_HELPERS', '\n'.join(definition(native, x) for x in (
    'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)',
    'std::string owned(XpAppText value)')))
source = replace_once(source, '// ACTUAL_MUTEX_METHODS', '\n'.join(definition(package, x) for x in (
    'void BookMetadataCache::lockReadCursor()', 'void BookMetadataCache::unlockReadCursor()')))
source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join(
    'value.' + x + ' = ' + native_leaf(native, x) + ';' for x in (
        'file_open_read', 'file_read', 'file_close', 'file_seek', 'file_size', 'cache_lock', 'cache_unlock',
        'file_open_write', 'file_write', 'file_flush', 'path_remove', 'path_rename', 'path_mkdir')))
cpp = args.build / 'unit1.cpp'
writer = (ROOT / 'test/package_golden/PackageWriterTest.cpp').read_text()
writer = writer.split('namespace {', 1)[1].split('XpPkgSpan span(', 1)[0]
writer = writer.replace('Storage', 'WriterStorage')
writer = writer.replace('return index + 10;', 'return index == 0 ? 0xffffffffu : 1;')
source += ('\n#include <array>\n#include <deque>\n#include <algorithm>\n#include <fstream>\n'
           '#include <xp_book.h>\nnamespace WriterFixture {\n' + writer + '\n}\n')
source += (HERE / 'writer_boundary.cpp').read_text()
# Compile the actual native consumers of the screenshot and exit ABI values.
boundaries = (HERE / 'boundary_consumers.cpp').read_text()
for marker, path, signature in [
    ('// ACTUAL_SCREENSHOT_FILENAME', 'src/util/ScreenshotUtil.cpp', 'void ScreenshotUtil::buildFilename'),
    ('// ACTUAL_SCREENSHOT_INFO', 'src/activities/ActivityManager.cpp', 'ScreenshotInfo ActivityManager::getScreenshotInfo'),
    ('// ACTUAL_XTC_PROGRESS', 'src/activities/reader/XtcReaderActivity.cpp', 'ScreenshotInfo XtcReaderActivity::getScreenshotInfo'),
    ('// ACTUAL_EXIT', 'src/activities/reader/RustEpubResources.cpp', 'void emitExit')]:
    boundaries = replace_once(boundaries, marker, definition((ROOT / path).read_text(), signature))
main = (ROOT / 'src/main.cpp').read_text()
start = main.index('  pluginevents::Var vars[', main.index('static void deliverSleepPluginEvents'))
end = main.index('  if (!allowNetwork)', start)
boundaries = replace_once(boundaries, '// ACTUAL_SLEEP', main[start:end])
source += boundaries
cpp.write_text(source + (HERE / 'unit1.cpp').read_text())
binary = args.build / 'unit1'
cmd = ['c++', '-std=c++20', '-O0', '-g', '-UNDEBUG', '-DXP_FFI_HOST_FAULT_INJECTION',
       '-DXP_SD_FONT_CATALOG',
       '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
       *[f'-I{ROOT / x}' for x in ('lib/Memory', 'test/reader_lifecycle',
         'test/chapter_html_slim_parser/stubs', 'rust/crates/xp-ffi/include', 'test/reader_harden', 'src')],
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
def cache_bytes(sizes, toc_count, empty=False):
    records = [field(b'' if empty else b'x' * 40) + struct.pack('<Ih', size, -1) for size in sizes]
    records += [(field(b'' if empty else b'T' * 40) + field(b'' if empty else b'x' * 40)
                 + field(b'') + struct.pack('<Bh', 0, -1))] * toc_count
    position = 29 + 4 * len(records)
    offsets = []
    for record in records:
        offsets.append(position)
        position += len(record)
    return (struct.pack('<BIHH', 10, 29, len(sizes), toc_count) + metadata
            + b''.join(struct.pack('<I', offset) for offset in offsets) + b''.join(records))

cases += [('boundary', cache_bytes(sizes, toc_count, empty=True), sizes[-1] if sizes else 0)
          for sizes, toc_count in [([], 0), ([], 1), ([0], 0), ([0xffffffff, 0, 32], 1),
                                   ([0] * 65535, 65535)]]
cases += [('boundary', data + b'trailing', 12), ('policy', data, 0),
          ('cost', cache_bytes(list(range(1, 61)), 60), 0),
          ('cost', cache_bytes(list(range(1, 1001)), 1000), 0), ('writer', data, 0)]
# Zero-filled string bodies make an in-file grow/shrink look like valid fields
# unless the query is bounded by its next LUT target.
for kind, row in [('toc', field(bytes(15)) + field(b'') + field(b'') + struct.pack('<Bh', 0, 0)),
                  ('spine', field(bytes(15)) + struct.pack('<Ih', 0, 0))]:
    body = (struct.pack('<BIHH', 10, 29, 3 if kind == 'spine' else 0, 3 if kind == 'toc' else 0)
            + metadata + struct.pack('<III', 41, 41 + len(row), 41 + 2 * len(row)) + row * 3)
    if kind == 'spine':
        body += bytes(100)  # tolerated trailing bytes keep sequential open independent of row queries
    for index in [0, 1]:
        for length in [0, 15 + len(row)]:
            damaged = bytearray(body)
            struct.pack_into('<I', damaged, 41 + index * len(row), length)
            cases.append(('row-' + kind, damaged, index))
    for index in [0, 1, 2]:
        cases.append(('query-' + kind, body, index))

unit1 = cases
unit2 = [('unit2-' + mode, cache_bytes([100 * i for i in range(1, 101)], 0), 0)
         for mode in ['size', 'spine', 'progress', 'mapping', 'progress-valid', 'mapping-valid', 'save', 'save-valid',
                      'walk-name', 'walk-header', 'walk-open', 'walk-oom', 'walk-valid',
                      'css-name', 'css-header', 'css-open', 'css-oom', 'css-alloc', 'css-valid']]
unit2 += [('unit2-boundary-' + kind, cache_bytes([100 * i for i in range(1, 101)], 0), 0) for kind in ('initial', 'known')]
rows_data = bytearray(cache_bytes([100, 200, 300], 2))
first_row = struct.unpack_from('<I', rows_data, lut)[0]
struct.pack_into('<h', rows_data, first_row + 4 + 40 + 4, 0)
unit3 = [('unit3-' + kind + '-' + fault, bytes(rows_data), 0)
         for kind in ['spine', 'toc', 'index', 'href', 'reference']
         for fault in ['valid', 'short', 'seek', 'alloc']]
cases = unit1 if args.unit == '1' else unit2 if args.unit == '2' else unit3 if args.unit == '3' else unit1 + unit2 + unit3
results = []
for number, (mode, content, offset) in enumerate(cases):
    folder = args.build / f'{number:03d}-{mode}'
    folder.mkdir(exist_ok=True)
    (folder / 'book.bin').write_bytes(content)
    if mode.startswith('text'):
        (folder / 'source.txt').write_text('new source content' if mode == 'text' else '')
        (folder / 'html').mkdir(exist_ok=True)
        (folder / 'html/0.html').write_text('<!-- TXT_CACHE_VERSION: 1 --><html><body></body></html>')
    if mode == 'seek' or mode.startswith('unit2-'):
        with zipfile.ZipFile(folder / 'source.epub', 'w') as z:
            z.writestr('chapter.xhtml', b'HELLO READERS')
            if mode.startswith('unit2-'):
                z.writestr('a.css', b'p { margin-top: 4px; }')
                z.writestr('b.css', b'p { margin-bottom: 9px; }')
        offset = 30 + len('chapter.xhtml')
        if mode.startswith('unit2-'):
            archive_bytes = (folder / 'source.epub').read_bytes()
            central = archive_bytes.find(b'PK\x01\x02')
            second = archive_bytes.find(b'PK\x01\x02', central + 4)
            third = archive_bytes.find(b'PK\x01\x02', second + 4)
            offset = third if mode.endswith('header') else third + 46
    command = [str(binary), mode, str(folder), str(offset)]
    run = subprocess.run(command, capture_output=True)
    (folder / 'run.log').write_bytes(run.stdout + run.stderr)
    results.append(dict(mode=mode, command=command, returncode=run.returncode))
(args.build / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
failures = [r for r in results if r['returncode']]
print(f'{len(results) - len(failures)}/{len(results)} strict shipping-reader cases passed')
assert not failures, [(r['mode'], r['returncode']) for r in failures]
