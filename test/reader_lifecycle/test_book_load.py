"""Complete native Epub::load and Rust ReaderBook load on identical physical ZIPs."""
import argparse
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import zipfile
from book_items_composition import compose
from test_popup_resource import definition, replace_once
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
spec = importlib.util.spec_from_file_location('package_frozen', ROOT / 'test/package_golden/run.py')
frozen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frozen)


def build(output, allocation_faults=False):
    stubs = output / 'stubs'; stubs.mkdir(exist_ok=True)
    (stubs / 'Txt.h').write_text((ROOT / 'lib/Txt/Txt.h').read_text())
    storage = (ROOT / 'test/book_io/epub-stubs/HalStorage.h').read_text()
    storage = replace_once(storage, 'size_t read(void* buffer, size_t count)', 'int read(void* buffer, size_t count)')
    storage = replace_once(storage, 'bool remove(const std::string& path) {', 'bool remove(const std::string& path) { printf("remove %s\\n", path.c_str());')
    storage = replace_once(storage, 'bool removeDir(const char* path) {', 'bool removeDir(const char* path) { printf("removeDir %s\\n", path);')
    storage = replace_once(storage, '  bool seek(size_t pos) {', '  bool seek(size_t pos) { if (!file_) { printf("nonio closed-seek %s %zu\\n", path_.c_str(), pos); return false; }')
    if allocation_faults:
        storage = replace_once(storage, 'namespace HalFileTest {',
                               'namespace HalFileTest {\ninline size_t liveFiles = 0, liveObjects = 0;')
        storage = replace_once(storage, '  HalFile() = default;',
                               '  HalFile() { ++HalFileTest::liveObjects; }')
        storage = replace_once(storage, 'explicit HalFile(const char* path) { open',
                               'explicit HalFile(const char* path) { ++HalFileTest::liveObjects; open')
        storage = replace_once(storage, '~HalFile() { close(); }',
                               '~HalFile() { close(); --HalFileTest::liveObjects; }')
        storage = replace_once(storage, '    file_ = std::fopen(path, mode);',
                               '    file_ = std::fopen(path, mode);\n    if (file_) ++HalFileTest::liveFiles;')
        storage = replace_once(storage, '    const bool ok = std::fclose(file_) == 0;',
                               '    --HalFileTest::liveFiles;\n    const bool ok = std::fclose(file_) == 0;')
        protection = (ROOT / 'test/book_io/epub-stubs/ContentProtection.h').read_text()
        protection = replace_once(protection, 'struct ContentDecryptor {',
                                  'inline size_t liveDecryptors = 0;\nstruct ContentDecryptor {\n'
                                  '  ContentDecryptor() { ++liveDecryptors; }\n'
                                  '  ~ContentDecryptor() { --liveDecryptors; }')
        (stubs / 'ContentProtection.h').write_text(protection)
    (stubs / 'HalStorage.h').write_text(storage)
    # Preserve the production 256-byte serial formatter, including its prefix and newline.
    logging_cpp = (ROOT / 'lib/Logging/Logging.cpp').read_text()
    logging_header = (ROOT / 'lib/Logging/Logging.h').read_text()
    logging = """#pragma once
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#define MAX_ENTRY_LEN 256
#define FREEINK_LOG_TRANSPORT 0
#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 1
uint32_t millis();
inline void addToLogRingBuffer(const char*) {}
inline struct BookSerial { void print(const char* value) { fputs(value, stdout); } } logSerial;
""" + 'inline ' + definition(logging_cpp, 'void logPrintf(') + '\n'
    logging += logging_header[logging_header.index('#ifdef ENABLE_SERIAL_LOG'):logging_header.index('std::string getLastLogs();')]
    (stubs / 'Logging.h').write_text(logging)
    common = (HERE / 'book_items_oracle.cpp').read_text().split('extern "C" int64_t xp_epub_test_items')[0]
    common = replace_once(common, definition(common, 'class Txt {') + ';', '')
    common = replace_once(common, definition(common, 'struct BookMetadataCache {') + ';', '')
    common = replace_once(common, 'bool readItemContentsToStream(const std::string&, Print&, size_t, bool) const;',
                          'bool readItemContentsToStream(const std::string&, Print&, size_t, bool = false) const;')
    common = replace_once(common, '  struct ItemContext;', '''  std::string cachePath, contentBasePath, tocNcxItem, tocNavItem, protectionError;
  std::vector<std::string> cssFiles;
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  std::unique_ptr<CssParser> cssParser;
  const std::string& getCachePath() const { return cachePath; }
  struct DocumentContext;
  static XpBookDocumentServices documentServices(DocumentContext&);
  bool findContentOpfFile(std::string*, ZipFile* = nullptr) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata&, bool = false, bool = false, ZipFile* = nullptr);
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  void discoverCssFilesFromZip();
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus) const;
  bool openProtection();
  bool load(bool, bool);
  XpBookMetadataServices cacheServices() const;
  void setupCacheDir() const;
  struct ItemContext;''')
    common = replace_once(common, 'static const XpAppResources resources', '// APP_CSTR_HELPER\nstatic const XpAppResources resources')
    source = replace_once((HERE / 'book_load_main.cpp').read_text(), '// COMMON_ITEM_COMPOSITION', common)
    source = compose(source, ('file_open_write', 'file_write', 'path_remove', 'path_rename', 'path_remove_tree', 'path_mkdir', 'heap_snapshot', 'cache_lock', 'cache_unlock'), ('path_exists',))
    epub = (ROOT / 'lib/Epub/Epub.cpp').read_text()
    txt = (ROOT / 'lib/Txt/Txt.cpp').read_text()
    methods = [definition(epub, 'XpBookBorrow borrow(std::string_view text)'), definition(epub, 'std::string_view view(XpBookBorrow text)'), definition(epub, 'struct Epub::DocumentContext') + ';']
    methods += [definition(epub, signature) for signature in (
        'XpBookDocumentServices Epub::documentServices(', 'bool Epub::findContentOpfFile(', 'bool Epub::parseContentOpf(',
        'bool Epub::parseTocNcxFile()', 'bool Epub::parseTocNavFile()', 'void Epub::discoverCssFilesFromZip()',
        'CssParser::ParseResult Epub::parseCssFiles(', 'bool Epub::openProtection()', 'bool Epub::load(const bool',
        'XpBookMetadataServices Epub::cacheServices() const', 'void Epub::setupCacheDir() const')]
    methods += [definition(txt, signature) for signature in ('std::string Txt::findCompanionCoverImage(', 'void Txt::invalidateCache(', 'bool Txt::validateCache(', 'bool Txt::buildTxtCache(')]
    source = replace_once(source, '// ACTUAL_LOAD_METHODS', '\n'.join(methods))
    if allocation_faults:
        source = replace_once(source, '  if (fault && ++allocationAttempt ==',
                              '  ++allocationAttempt;\n  if (fault && allocationAttempt ==')
        source = replace_once(source, 'size_t xp_test_live_allocations();',
                              'size_t xp_test_live_allocations();\nvoid xp_test_fail_allocation_after(size_t);\n'
                              'size_t xp_test_allocation_attempts();')
        source = replace_once(source, 'static uint64_t queryRecords = 0, queryBytes = 0;',
                              'static uint64_t queryRecords = 0, queryBytes = 0;\nstatic size_t loadAttempts = SIZE_MAX;')
        source = replace_once(source, '  if (kind == 9) {\n    puts("queries");',
                              '  if (kind == 9) {\n    loadAttempts = xp_test_allocation_attempts();\n'
                              '    xp_test_fail_allocation_after(SIZE_MAX);\n    puts("queries");')
        source = replace_once(source, '  bool result;',
                              '  const char* allocationFault = std::getenv("BOOK_OWNER_ALLOC_FAIL");\n'
                              '  xp_test_fail_allocation_after(allocationFault ? std::strtoull(allocationFault, nullptr, 10) : SIZE_MAX);\n'
                              '  bool result;')
        source = replace_once(source, '  assert(fclose(queryStream) == 0);',
                              '  if (loadAttempts == SIZE_MAX) loadAttempts = xp_test_allocation_attempts();\n'
                              '  xp_test_fail_allocation_after(SIZE_MAX);\n'
                              '  printf("load-allocation-attempts %zu %zu\\n", loadAttempts, allocationAttempt);\n'
                              '  assert(HalFileTest::liveFiles == 0 && HalFileTest::liveObjects == 0);\n'
                              '  assert(freeink::content::liveDecryptors == 0);\n'
                              '  assert(fclose(queryStream) == 0);')
    cpp = output / 'oracle.cpp'; cpp.write_text(source)
    binary = output / 'oracle'
    includes = ['test/book_io/epub-stubs', 'lib/Epub', 'lib/Epub/Epub', 'lib/ZipFile', 'lib/Txt', 'lib/FsHelpers', 'lib/Utf8',
                'lib/Memory', 'lib/Serialization', 'lib/expat', 'lib/hal', 'lib/miniz/src', 'test/reader_lifecycle', 'test/ui_runtime',
                'test/chapter_html_slim_parser/stubs']
    sources = ['lib/Epub/Epub/PackagePlatform.cpp', 'lib/Epub/Epub/css/CssPlatform.cpp', 'lib/expat/ExpatErrors.cpp',
               'lib/ZipFile/ZipPlatform.cpp', 'lib/FsHelpers/FsHelpers.cpp', 'lib/Utf8/Utf8.cpp', 'lib/Txt/TxtToHtml.cpp',
               'lib/miniz/src/InflateStream.cpp', 'lib/Memory/BuildScratch.cpp', 'lib/hal/HalMemory.cpp']
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG', '-include', 'algorithm',
               '-DENABLE_SERIAL_LOG', '-DLOG_LEVEL=2', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', str(stubs),
               *[arg for path in includes for arg in ('-I', str(ROOT / path))],
               str(cpp), *[str(ROOT / path) for path in sources], *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode: raise RuntimeError(built.stderr.decode())
    return binary, command, source


QUERY_CAP = 4096


def capture(binary, case, mode, skip, fault, log_path, dump_path=None,
            source_path='source.epub', build_missing=True, env=None):
    """Hash complete length-framed records on a pipe; only retain a dump on mismatch replay."""
    read_fd, write_fd = os.pipe()
    digest = hashlib.sha256()
    observed = dict(bytes=0)
    errors = []

    def consume():
        try:
            with os.fdopen(read_fd, 'rb') as stream:
                dump = dump_path.open('wb') if dump_path else None
                try:
                    while chunk := stream.read(1024 * 1024):
                        digest.update(chunk)
                        observed['bytes'] += len(chunk)
                        if dump: dump.write(chunk)
                finally:
                    if dump: dump.close()
        except BaseException as error:
            errors.append(error)

    reader = threading.Thread(target=consume)
    reader.start()
    try:
        with log_path.open('wb') as log:
            process = subprocess.Popen(
                [str(binary), mode, source_path, 'cache', str(int(build_missing)), str(skip), str(fault), str(QUERY_CAP)],
                cwd=case, stdout=log, stderr=subprocess.PIPE, pass_fds=(write_fd,),
                env={**os.environ, **(env or {}), 'XP_BOOK_QUERY_FD': str(write_fd)})
            os.close(write_fd)
            write_fd = None
            try:
                _, stderr = process.communicate(timeout=300)
            except subprocess.TimeoutExpired:
                process.kill()
                _, stderr = process.communicate()
                log.write(stderr)
                raise
            log.write(stderr)
    finally:
        if write_fd is not None: os.close(write_fd)
        reader.join()
    assert not errors, errors
    assert process.returncode == 0, (case.name, mode, skip, fault, process.returncode, stderr[-4096:])
    data = log_path.read_bytes()
    summary = re.search(rb'^query-summary (\d+) (\d+)$', data, re.M)
    assert summary, (log_path, 'missing query summary')
    assert int(summary[2]) == observed['bytes'], (log_path, 'incomplete query stream')
    observed.update(sha256=digest.hexdigest(), records=int(summary[1]),
                    cap_hit=b'query-cap ' in data, cap=QUERY_CAP)
    return data, observed


def values(data):
    return [line for line in data.splitlines() if line.startswith((b'query-summary ', b'query-cap ', b'result '))]


def load_trace(data):
    trace = data.split(b'queries\n', 1)[0] if b'queries\n' in data else data
    return b'\n'.join(line for line in trace.splitlines()
                      if not line.startswith((b'nonio closed-seek ', b'query-summary ')))


def restore(cache, seed):
    shutil.rmtree(cache, ignore_errors=True)
    for filename, data in seed.items():
        target = cache / filename
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(bytes.fromhex(data))


def persisted_files(cache):
    return {str(path.relative_to(cache)): path.read_bytes().hex()
            for path in cache.rglob('*') if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--success-only', action='store_true',
                        help='CTest success matrix; omit for the full physical-fault campaign')
    args = parser.parse_args()
    output = Path(os.environ.get('XP_BOOK_LOAD_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-load-'))
    output.mkdir(parents=True, exist_ok=True)
    binary, command, source = build(output)
    cases = []
    for path in sorted((ROOT / 'test/epubs').glob('*.epub')):
        with zipfile.ZipFile(path) as archive:
            cases.append((path.stem, {name: archive.read(name) for name in archive.namelist() if not name.endswith('/')}))
    cases += [('synthetic-' + name, files) for name, files in frozen.synthetic()]
    results = []

    def save():
        (output / 'results.json').write_text(json.dumps(dict(
            command=command, cases=results, query_cap=QUERY_CAP, success_only=args.success_only,
            physical_faults_reached=sum(row.get('faults_reached', 0) for row in results),
            physical_faults_matched=sum(row.get('physical_faults', 0) for row in results),
            capped_fault_points=sum(row.get('capped_fault_points', 0) for row in results),
            generated_source_sha256=hashlib.sha256(source.encode()).hexdigest()), indent=2) + '\n')

    for name, files in cases:
        case = output / name
        case.mkdir(exist_ok=True)
        with zipfile.ZipFile(case / 'source.epub', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for path, data in files.items(): archive.writestr(path, data)
        cache = case / 'cache'
        for skip in (0, 1):
            seed = {}
            for stage in ('cold', 'warm'):
                captures, persisted, queries = [], [], []
                for mode in ('native', 'rust'):
                    restore(cache, seed)
                    data, query = capture(binary, case, mode, skip, 0, case / f'{mode}-{stage}-{skip}.log')
                    captures.append(data)
                    queries.append(query)
                    persisted.append(persisted_files(cache))
                record = dict(name=name, stage=stage, skip_css=skip,
                              values_equal=values(captures[0]) == values(captures[1]),
                              queries=queries, query_equal=queries[0] == queries[1],
                              files_equal=persisted[0] == persisted[1],
                              load_trace_equal=load_trace(captures[0]) == load_trace(captures[1]),
                              raw_trace_equal=captures[0] == captures[1], faults_reached=0,
                              physical_faults=0, capped_fault_points=0)
                results.append(record)
                save()
                assert all(record[key] for key in ('values_equal', 'query_equal', 'files_equal', 'load_trace_equal')), record
                if args.success_only:
                    seed = persisted[0]
                    continue
                trace = load_trace(captures[0])
                steps = max([int(value) for value in re.findall(rb'^io (\d+) ', trace, re.M)] or [0])
                faults = case / f'faults-{stage}-{skip}'
                faults.mkdir(exist_ok=True)
                with (faults / 'queries.jsonl').open('w') as query_log:
                    for fault in range(1, steps + 1):
                        record['faults_reached'] += 1
                        save()
                        fault_captures, fault_files, fault_queries = [], [], []
                        for mode in ('native', 'rust'):
                            restore(cache, seed)
                            data, query = capture(binary, case, mode, skip, fault, faults / f'{mode}-{fault}.log')
                            fault_captures.append(data)
                            fault_queries.append(query)
                            fault_files.append(persisted_files(cache))
                        reached = [bool(re.search(rb'io ' + str(fault).encode() + rb' [^\n]* FAIL(?:\n|$)',
                                                  load_trace(data))) for data in fault_captures]
                        equal = dict(reached=all(reached), values=values(fault_captures[0]) == values(fault_captures[1]),
                                     queries=fault_queries[0] == fault_queries[1],
                                     files=fault_files[0] == fault_files[1],
                                     load_trace=load_trace(fault_captures[0]) == load_trace(fault_captures[1]))
                        query_log.write(json.dumps(dict(fault=fault, native=fault_queries[0], rust=fault_queries[1], equal=equal)) + '\n')
                        query_log.flush()
                        if not all(equal.values()):
                            for mode in ('native', 'rust'):
                                restore(cache, seed)
                                capture(binary, case, mode, skip, fault, faults / f'{mode}-{fault}-replay.log',
                                        faults / f'{mode}-{fault}-queries.bin')
                            raise AssertionError((name, stage, skip, fault, equal))
                        record['physical_faults'] += 1
                        record['capped_fault_points'] += int(any(query['cap_hit'] for query in fault_queries))
                        save()
                print(f'{name}/{stage}/skip={skip}: {steps} physical fault points matched', flush=True)
                seed = persisted[0]
    print(f'Complete native/Rust load: {len(results)} success pairs; '
          f'{sum(row["physical_faults"] for row in results)} physical faults match complete query digests.')


if __name__ == '__main__': main()
