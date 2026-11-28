"""Pair native CSS file loading with Rust owners; tied keys use approved target ordering."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from book_items_composition import compose
from test_popup_resource import definition, replace_once

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


# Order of the tied-18 case in the C3 and S3 libstdc++ std::sort implementations.
TIED_18_TARGET_ORDER = [9, 17, 16, 15, 14, 13, 12, 11, 10, 0, 8, 7, 6, 5, 4, 3, 2, 1]
TIED_18_PATHS = ['b.css' if index == 1 else 'a.css' for index in range(18)]


def fnv(path):
    value = 14695981039346656037
    for byte in path.encode(): value = ((value ^ byte) * 1099511628211) % 2**64
    return value


def target_checks(output, require_compilers):
    entries = ','.join(f'{{{fnv(path)}ull,{index}}}' for index, path in enumerate(TIED_18_PATHS))
    expected = ','.join(map(str, TIED_18_TARGET_ORDER))
    source = f'''#include <algorithm>
#include <array>
#include <cstdint>
struct Entry {{ uint64_t hash; unsigned index; }};
constexpr auto sorted() {{ std::array<Entry,{len(TIED_18_PATHS)}> values{{{{{entries}}}}};
 std::sort(values.begin(),values.end(),[](const auto& a,const auto& b){{return a.hash<b.hash;}});return values; }}
constexpr std::array<unsigned,{len(TIED_18_PATHS)}> expected{{{expected}}};
static_assert([]{{auto values=sorted();for(unsigned i=0;i<values.size();++i)if(values[i].index!=expected[i])return false;return true;}}());
'''
    cpp = output / 'target-sort.cpp'
    cpp.write_text(source)
    missing = []
    for name, executable in [('c3', 'riscv32-esp-elf-g++'), ('s3', 'xtensa-esp32s3-elf-g++')]:
        compiler = shutil.which(executable)
        if compiler is None:
            missing.append(executable)
            continue
        command = [compiler, '-std=c++20', '-fsyntax-only', str(cpp)]
        ran = subprocess.run(command, capture_output=True)
        (output / (name + '.log')).write_bytes(ran.stdout + ran.stderr)
        assert ran.returncode == 0, ran.stderr.decode()
        print(f'{name} tied-key order checked with {compiler}.')
    if missing:
        status = 'Required' if require_compilers else 'Skipped'
        print(f'{status} ESP compilers unavailable on PATH: {", ".join(missing)}')
        return 1 if require_compilers else 77
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--target-order', action='store_true')
    mode.add_argument('--require-target-order', action='store_true')
    args = parser.parse_args()
    output = Path(os.environ.get('XP_BOOK_CSS_FILES_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-css-files-'))
    output.mkdir(parents=True, exist_ok=True)
    if args.target_order or args.require_target_order:
        return target_checks(output, args.require_target_order)
    stubs = output / 'stubs'; stubs.mkdir()
    storage = (ROOT / 'test/book_io/epub-stubs/HalStorage.h').read_text()
    storage = replace_once(storage, 'size_t read(void* buffer, size_t count)', 'int read(void* buffer, size_t count)')
    (stubs / 'HalStorage.h').write_text(storage)
    common = (HERE / 'book_items_oracle.cpp').read_text().split('extern "C" int64_t xp_epub_test_items')[0]
    common = replace_once(common, 'static void* itemAllocate(size_t length) {',
                          'static void* itemAllocate(size_t length) {\n  if (failDedup(length)) return nullptr;')
    common = replace_once(common, 'bool readItemContentsToStream(const std::string&, Print&, size_t, bool) const;',
                          'bool readItemContentsToStream(const std::string&, Print&, size_t, bool = false) const;')
    common = replace_once(common, '  struct ItemContext;', '''  std::string cachePath, contentBasePath;
  std::vector<std::string> cssFiles;
  std::unique_ptr<CssParser> cssParser;
  const std::string& getCachePath() const { return cachePath; }
  void discoverCssFilesFromZip();
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus) const;
  struct ItemContext;''')
    common = replace_once(common, 'static const XpAppResources resources', '// APP_CSTR_HELPER\nstatic const XpAppResources resources')
    source = replace_once((HERE / 'book_css_files_main.cpp').read_text(), '// COMMON_ITEM_COMPOSITION', common)
    source = compose(source, ('file_open_write', 'file_write', 'path_remove', 'path_rename', 'heap_snapshot'), ('path_exists',))
    epub = (ROOT / 'lib/Epub/Epub.cpp').read_text()
    methods = definition(epub, 'void Epub::discoverCssFilesFromZip()') + '\n' + definition(epub, 'CssParser::ParseResult Epub::parseCssFiles(')
    old_sort = '''std::sort(ctx.entries.get(), ctx.entries.get() + paths.size(),
                      [](const auto& a, const auto& b) { return a.pathHash < b.pathHash; });'''
    methods = replace_once(methods, old_sort, 'hostOnlyTargetCssSort(ctx.entries.get(), paths.size());')
    source = replace_once(source, '// ACTUAL_CSS_METHODS', definition(epub, 'XpBookBorrow borrow(std::string_view text)') + '\n' + methods)
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    includes = ['test/book_io/epub-stubs', 'lib/Epub', 'lib/ZipFile', 'lib/Txt', 'lib/FsHelpers', 'lib/Utf8',
                'lib/Memory', 'lib/hal', 'lib/miniz/src', 'test/reader_lifecycle', 'test/ui_runtime',
                'test/chapter_html_slim_parser/stubs']
    sources = ['lib/Epub/Epub/css/CssPlatform.cpp', 'lib/ZipFile/ZipPlatform.cpp', 'lib/FsHelpers/FsHelpers.cpp',
               'lib/Utf8/Utf8.cpp', 'lib/Txt/TxtToHtml.cpp', 'lib/miniz/src/InflateStream.cpp',
               'lib/Memory/BuildScratch.cpp', 'lib/hal/HalMemory.cpp']
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG', '-include', 'algorithm',
               '-DENABLE_SERIAL_LOG', '-DLOG_LEVEL=2', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(stubs),
               *[arg for path in includes for arg in ('-I', str(ROOT / path))],
               str(cpp), *[str(ROOT / path) for path in sources], *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    files = {'a.css': b'p { margin-top: 4px; }', 'b.css': b'p { margin-top: 4px; }',
             'OPS/discovered.CSS': b'body { text-align: center; }', 'other.css': b'p { margin-top: 8px; }',
             'large.css': b' ' * 131073}
    with zipfile.ZipFile(output / 'book.epub', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path, data in files.items(): archive.writestr(path, data)
    cases = [('empty', [], False, False, 100000), ('one', ['a.css'], False, False, 100000),
             ('identical-content', ['a.css', 'b.css'], False, False, 100000),
             ('two', ['a.css', 'other.css'], False, False, 100000),
             ('missing', ['missing.css'], False, False, 100000),
             ('discover', [], True, False, 100000), ('low-heap', ['a.css'], False, False, 65535),
             ('threshold', ['a.css'], False, False, 65536), ('oversize', ['large.css'], False, False, 100000),
             ('partial', ['large.css', 'a.css'], False, True, 100000),
             ('old-cache-error', ['missing.css'], False, False, 100000),
             ('dedup-oom', ['a.css', 'b.css'], False, False, 100000),
             ('tied-18', TIED_18_PATHS, False, False, 100000)]
    results = []
    for name, paths, discover, partial, heap in cases:
        case = output / name; case.mkdir()
        args = ['book.epub', 'cache', '0', str(int(discover)), str(int(partial)), 'OPS/', str(heap), *paths]
        tied_keys = len(set(map(fnv, paths))) != len(paths)
        options = {'CSS_DEDUP_FAIL': '1'} if name == 'dedup-oom' else {}
        if tied_keys:
            sorted_result = subprocess.run([str(binary), 'sort', *args], cwd=output, capture_output=True, check=True)
            permutation = [int(value) for value in sorted_result.stdout.split()]
            assert name == 'tied-18'
            assert permutation == TIED_18_TARGET_ORDER, permutation
            permutation_path = case / 'permutation.txt'
            permutation_path.write_text(' '.join(map(str, TIED_18_TARGET_ORDER)))
            options['CSS_TARGET_PERMUTATION'] = str(permutation_path)
        steps = 0
        required_discovery_steps = 0
        for fault in range(10000):
            captures = []
            args[2] = str(fault)
            for mode in ('native', 'rust'):
                cache = output / 'cache'
                if cache.exists(): shutil.rmtree(cache)
                cache.mkdir()
                if name in ('partial', 'old-cache-error'):
                    (cache / 'css_rules.cache').write_bytes(b'previous cache remains intact')
                ran = subprocess.run([str(binary), mode, *args], cwd=output, env={**os.environ, **options}, capture_output=True, timeout=60)
                (case / f'{mode}-{fault}.log').write_bytes(ran.stdout + ran.stderr)
                assert ran.returncode == 0, (name, mode, fault, ran.stderr.decode())
                captured = {str(p.relative_to(cache)): p.read_bytes().hex() for p in sorted(cache.rglob('*')) if p.is_file()}
                captures.append((ran.stdout, captured))
            if discover and 0 < fault <= required_discovery_steps:
                # Native discovery used to parse/save an incomplete stylesheet list.
                before, after = captures[0][0], captures[1][0]
                marker = b' FAIL\n'
                assert marker in before and marker in after
                assert before.split(marker, 1)[0] == after.split(marker, 1)[0]
                assert b'result 2\n' in after and b'CSS files to parse:' not in after
                assert int(re.search(rb'steps (\d+)', after)[1]) == fault
                assert captures[1][1] == {}, (name, fault, captures[1][1])
            else:
                assert captures[0] == captures[1], (name, fault)
            if fault == 0:
                steps = int(re.search(rb'steps (\d+)', captures[0][0])[1])
                if discover:
                    with zipfile.ZipFile(output / 'book.epub') as archive:
                        end = archive.start_dir + sum(46 + len(row.filename.encode()) + len(row.extra)
                                                      + len(row.comment) for row in archive.infolist())
                    # The final end probe is retained; only declared entries are required.
                    required_discovery_steps = int(re.search(
                        rb'io (\d+) seek book.epub ' + str(end).encode() + rb' 0\n', captures[0][0])[1]) - 1
            if fault >= steps: break
        if name == 'tied-18':
            trace = (case / 'rust-0.log').read_text()
            assert 'Parsing CSS file: b.css' in trace
            tied_reads = [int(v) for v in re.findall(r'io (\d+) read book.epub .*', trace)]
            assert tied_reads and any('Could not read CSS file: b.css' in (case / f'rust-{fault}.log').read_text() for fault in range(1, steps + 1))
        results.append(dict(name=name, host_only_target_sort_adaptation=tied_keys, physical_faults=steps, rejected_discovery_faults=required_discovery_steps))
    (output / 'results.json').write_text(json.dumps(dict(command=command, cases=results,
        generated_source_sha256=hashlib.sha256(source.encode()).hexdigest()), indent=2) + '\n')
    print(f'{len(results)} composed CSS cases with every reached storage fault pass.')
    print('Tied-key order matches the recorded C3/S3 order.')


if __name__ == '__main__': sys.exit(main())
