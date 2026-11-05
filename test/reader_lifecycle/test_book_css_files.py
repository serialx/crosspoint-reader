"""Pair native CSS file loading with Rust owners; tied keys use approved target ordering."""
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


def fnv(path):
    value = 14695981039346656037
    for byte in path.encode(): value = ((value ^ byte) * 1099511628211) % 2**64
    return value


def target_checks(case, paths, permutation):
    entries = ','.join('{' + str(fnv(path)) + 'ull,' + str(index) + '}' for index, path in enumerate(paths))
    expected = ','.join(map(str, permutation))
    source = f'''#include <algorithm>
#include <array>
#include <cstdint>
struct Entry {{ uint64_t hash; unsigned index; }};
constexpr auto sorted() {{ std::array<Entry,{len(paths)}> values{{{{{entries}}}}};
 std::sort(values.begin(),values.end(),[](const auto& a,const auto& b){{return a.hash<b.hash;}});return values; }}
constexpr std::array<unsigned,{len(paths)}> expected{{{expected}}};
static_assert([]{{auto values=sorted();for(unsigned i=0;i<values.size();++i)if(values[i].index!=expected[i])return false;return true;}}());
'''
    cpp = case / 'target-sort.cpp'; cpp.write_text(source)
    checks = []
    for name, compiler in [('c3', '/Users/serialx/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-g++'),
                           ('s3', '/Users/serialx/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++')]:
        command = [compiler, '-std=c++20', '-fsyntax-only', str(cpp)]
        ran = subprocess.run(command, capture_output=True)
        (case / (name + '.log')).write_bytes(ran.stdout + ran.stderr)
        assert ran.returncode == 0, ran.stderr.decode()
        checks.append(dict(name=name, command=command, returncode=ran.returncode))
    return checks


def main():
    output = Path(os.environ.get('XP_BOOK_CSS_FILES_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-css-files-'))
    output.mkdir(parents=True, exist_ok=True)
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
    tied = ['a.css' if i != 1 else 'b.css' for i in range(18)]
    cases = [('empty', [], False, False, 100000), ('one', ['a.css'], False, False, 100000),
             ('identical-content', ['a.css', 'b.css'], False, False, 100000),
             ('two', ['a.css', 'other.css'], False, False, 100000),
             ('missing', ['missing.css'], False, False, 100000),
             ('discover', [], True, False, 100000), ('low-heap', ['a.css'], False, False, 65535),
             ('threshold', ['a.css'], False, False, 65536), ('oversize', ['large.css'], False, False, 100000),
             ('partial', ['large.css', 'a.css'], False, True, 100000),
             ('old-cache-error', ['missing.css'], False, False, 100000),
             ('dedup-oom', ['a.css', 'b.css'], False, False, 100000),
             ('tied-18', tied, False, False, 100000)]
    results = []
    for name, paths, discover, partial, heap in cases:
        case = output / name; case.mkdir()
        args = ['book.epub', 'cache', '0', str(int(discover)), str(int(partial)), 'OPS/', str(heap), *paths]
        tied_keys = len(set(map(fnv, paths))) != len(paths)
        options = {'CSS_DEDUP_FAIL': '1'} if name == 'dedup-oom' else {}
        checks = []
        if tied_keys:
            sorted_result = subprocess.run([str(binary), 'sort', *args], cwd=output, capture_output=True, check=True)
            permutation = [int(value) for value in sorted_result.stdout.split()]
            checks = target_checks(case, paths, permutation)
            permutation_path = case / 'permutation.txt'; permutation_path.write_text(' '.join(map(str, permutation)))
            options['CSS_TARGET_PERMUTATION'] = str(permutation_path)
        steps = 0
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
            assert captures[0] == captures[1], (name, fault)
            if fault == 0: steps = int(re.search(rb'steps (\d+)', captures[0][0])[1])
            if fault >= steps: break
        if name == 'tied-18':
            trace = (case / 'rust-0.log').read_text()
            assert 'Parsing CSS file: b.css' in trace
            tied_reads = [int(v) for v in re.findall(r'io (\d+) read book.epub .*', trace)]
            assert tied_reads and any('Could not read CSS file: b.css' in (case / f'rust-{fault}.log').read_text() for fault in range(1, steps + 1))
        results.append(dict(name=name, host_only_target_sort_adaptation=tied_keys, target_checks=checks, physical_faults=steps))
    (output / 'results.json').write_text(json.dumps(dict(command=command, cases=results,
        generated_source_sha256=hashlib.sha256(source.encode()).hexdigest()), indent=2) + '\n')
    print(f'{len(results)} composed CSS cases with every reached storage fault pass; tied-key host adaptations are explicit and checked on C3/S3.')


if __name__ == '__main__': main()
