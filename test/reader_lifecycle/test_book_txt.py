"""Run the unchanged TXT stream oracle through owned file/buffer adapters."""
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


def validation(output, native, app):
    txt = (ROOT / 'lib/Txt/Txt.cpp').read_text()
    html = (ROOT / 'lib/Txt/TxtToHtml.cpp').read_text()
    source = (HERE / 'book_txt_validate.cpp').read_text()
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', '\n'.join(
        definition(native, signature) for signature in (
            'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)')))
    source = replace_once(source, '// ACTUAL_CSTR_HELPER', definition(app, 'const char* cstr(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_RESOURCES', 'value.path_exists = ' + native_leaf(app, 'path_exists') + ';')
    source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join(
        'value.' + field + ' = ' + native_leaf(native, field) + ';'
        for field in ('file_open_read', 'file_read', 'file_size', 'file_close', 'path_remove_tree', 'log', 'log_enabled')))
    source = replace_once(source, '// ACTUAL_TAG', definition(html, 'const char* TxtToHtml::cacheVersionTag(std::string_view filename)'))
    source = replace_once(source, '// ACTUAL_INVALIDATE', definition(txt, 'void Txt::invalidateCache(const std::string& cachePath)'))
    source = replace_once(source, '// ACTUAL_VALIDATE', definition(txt, 'bool Txt::validateCache(const std::string& filepath, const std::string& cachePath, size_t cachedSize)'))
    cpp = output / 'validate.cpp'
    cpp.write_text(source)
    binary = output / 'validate'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               str(cpp), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'validate-compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(binary)], capture_output=True, timeout=120)
    (output / 'validate-run.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode:
        raise RuntimeError(ran.stderr.decode())
    (output / 'validate-results.json').write_text(json.dumps(dict(
        command=command, returncode=ran.returncode, cases=864,
        source_sha256=hashlib.sha256(source.encode()).hexdigest(),
        native_txt_sha256=hashlib.sha256(txt.encode()).hexdigest(),
        archive_sha256=hashlib.sha256(Path(next(arg for arg in command if arg.endswith('/libxp_ffi.a'))).read_bytes()).hexdigest(),
    ), indent=2) + '\n')
    print(ran.stdout.decode().strip())


def build_cache(output, native, app):
    directory = output / 'build'
    directory.mkdir()
    stubs = directory / 'stubs'
    stubs.mkdir()
    storage = (ROOT / 'test/chapter_html_slim_parser/stubs/HalStorage.h').read_text()
    storage = replace_once(storage, '  bool remove(const std::string& path)', '  bool removeDir(const char* path) { return std::filesystem::remove_all(path) != 0; }\n  bool remove(const std::string& path)')
    (stubs / 'HalStorage.h').write_text(storage)
    txt = (ROOT / 'lib/Txt/Txt.cpp').read_text()
    native_source = '''#include <Epub/BookMetadataCache.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Utf8.h>
#include <Txt.h>
#include <optional>
#include <cassert>
// ACTUAL_TXT_BUILD
int main(int argc, char** argv) {
  assert(argc == 3);
  auto cache = std::make_unique<BookMetadataCache>(argv[2]);
  return Txt::buildTxtCache(argv[1], argv[2], cache) ? 0 : 1;
}
'''
    native_source = replace_once(native_source, '// ACTUAL_TXT_BUILD', '\n'.join(definition(txt, signature) for signature in (
        'std::string Txt::findCompanionCoverImage(', 'void Txt::invalidateCache(', 'bool Txt::buildTxtCache(')))
    rust_source = (HERE / 'book_package_oracle.cpp').read_text().split('extern "C" uint32_t xp_epub_test_package')[0]
    rust_source += '''extern "C" uint8_t xp_epub_test_txt_build(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText);
int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string path = argv[1], cache = argv[2];
  const bool ok = xp_epub_test_txt_build(&resources, &leaves, text(path), text(cache));
  assert(nativeLive == 0 && xp_test_live_allocations() == 0 && liveFiles == 0);
  return ok ? 0 : 1;
}
'''
    rust_source = replace_once(rust_source, '// ACTUAL_CSTR_HELPER', definition(app, 'const char* cstr(XpAppText value)'))
    rust_source = replace_once(rust_source, '// ACTUAL_TEXT_HELPERS', '\n'.join(definition(native, signature) for signature in (
        'XpAppText text(const std::string& value)', 'const char* pathText(XpAppText value)', 'std::string owned(XpAppText value)')))
    rust_source = replace_once(rust_source, '// ACTUAL_ALLOCATOR_LEAVES', '\n'.join('value.' + field + ' = ' + native_leaf(app, field) + ';' for field in ('buffer_allocate', 'buffer_release', 'path_exists')))
    rust_source = replace_once(rust_source, '// ACTUAL_LEAVES', '\n'.join('value.' + field + ' = ' + native_leaf(native, field) + ';' for field in (
        'file_open_read', 'file_open_write', 'file_read', 'file_write', 'file_size', 'file_seek', 'file_close', 'path_remove', 'path_remove_tree', 'path_mkdir', 'cache_lock', 'cache_unlock')) + '\nvalue.log_enabled = [](uint8_t) -> uint8_t { return 0; };')
    commands = []
    for name, source in [('native', native_source), ('rust', rust_source)]:
        cpp = directory / (name + '.cpp')
        cpp.write_text(source)
        command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG', '-ftrivial-auto-var-init=zero',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION', '-I', str(stubs)]
        includes = ['test/xml_oracle/stubs', 'lib/Epub', 'lib/Epub/Epub', 'lib/Epub/Epub/parsers', 'lib/FsHelpers', 'lib/Memory',
                    'lib/Serialization', 'lib/Utf8', 'lib/XmlParserUtils', 'test/fs_helpers/stubs',
                    'test/book_metadata_cache/stubs', 'test/chapter_html_slim_parser/stubs', 'test/content_opf_parser/stubs',
                    'lib/expat', 'lib/Txt', 'test/reader_lifecycle', 'test/ui_runtime']
        for include in includes:
            command += ['-I', str(ROOT / include)]
        sources = [cpp]
        if name == 'native':
            sources += [ROOT / path for path in ('lib/Epub/Epub/PackagePlatform.cpp', 'lib/FsHelpers/FsHelpers.cpp', 'lib/Utf8/Utf8.cpp', 'lib/expat/ExpatErrors.cpp')]
        command += [str(path) for path in sources] + rust_link_flags() + ['-o', str(directory / name)]
        ran = subprocess.run(command, capture_output=True, timeout=120)
        (directory / (name + '-compile.log')).write_bytes(ran.stdout + ran.stderr)
        commands.append(dict(name=name, command=command, returncode=ran.returncode, source_sha256=hashlib.sha256(source.encode()).hexdigest()))
        (directory / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if ran.returncode:
            raise RuntimeError(ran.stderr.decode())
    cases = []
    for index, (title, length, cover) in enumerate([
        ('plain.txt', 0, None), ('a&<>.MD', 3, None), ('e\u0301.TXT', 8192, 'cover.jpg'),
        ('long.name.md', 9000, 'long.name.PNG'), ('한글.txt', 1, None), ('missing.txt', None, None),
    ]):
        folder = directory / str(index)
        folder.mkdir()
        path = folder / title
        if length is not None:
            path.write_bytes(b'x' * length)
        if cover:
            (folder / cover).write_bytes(b'cover')
        outputs = []
        statuses = []
        # Use the identical physical cache path for both runs.
        cache = folder / 'cache'
        for implementation in ('native', 'rust'):
            cache.mkdir()
            (cache / 'html').mkdir()
            (cache / 'html/0.html').write_bytes(b'old html')
            (cache / 'sections').mkdir()
            (cache / 'sections/0.bin').write_bytes(b'old section')
            ran = subprocess.run([str(directory / implementation), str(path), str(cache)], capture_output=True, timeout=60)
            (folder / (implementation + '.log')).write_bytes(ran.stdout + ran.stderr)
            statuses.append(ran.returncode)
            outputs.append({str(p.relative_to(cache)): p.read_bytes().hex() for p in cache.rglob('*') if p.is_file()})
            import shutil
            shutil.copytree(cache, folder / (implementation + '-cache'))
            shutil.rmtree(cache)
        assert statuses[0] == statuses[1] and outputs[0] == outputs[1], (index, statuses, outputs)
        cases.append(dict(index=index, title=title, status=statuses[0], files=outputs[0]))
    (directory / 'results.json').write_text(json.dumps(cases, indent=2) + '\n')
    print(f'{len(cases)} paired native/Rust TXT metadata builds have exact status/cache files/cleanup')


def main():
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    app = (ROOT / 'src/activities/RustAppResources.cpp').read_text()
    source = (HERE / 'book_txt_stream.cpp').read_text()
    source = replace_once(source, '// ACTUAL_TEXT_HELPERS', definition(native, 'const char* pathText(XpAppText value)'))
    source = replace_once(source, '// ACTUAL_LEAVES', '\n'.join(
        'value.' + field + ' = ' + native_leaf(native, field) + ';'
        for field in ('file_open_read', 'file_read', 'file_size', 'file_close')))
    source = replace_once(source, '// ACTUAL_ALLOCATORS', '\n'.join(
        'value.' + field + ' = ' + native_leaf(app, field) + ';'
        for field in ('buffer_allocate', 'buffer_release')))
    output = Path(os.environ.get('XP_BOOK_TXT_EVIDENCE_DIR') or tempfile.mkdtemp(prefix='book-txt-'))
    output.mkdir(parents=True, exist_ok=True)
    cpp = output / 'adapter.cpp'
    cpp.write_text(source)
    binary = output / 'oracle'
    oracle = ROOT / 'test/txt_to_html/StreamOracle.cpp'
    golden = ROOT / 'test/txt_to_html/stream-golden.txt.gz'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O0', '-g', '-UNDEBUG',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
               '-I', str(ROOT / 'lib/Memory'), '-I', str(HERE), '-I', str(ROOT / 'test/ui_runtime'),
               '-I', str(ROOT / 'lib/Txt'), '-I', str(ROOT / 'test/fs_helpers/stubs'),
               '-I', str(ROOT / 'test/chapter_html_slim_parser/stubs'),
               str(cpp), str(oracle), *rust_link_flags(), '-o', str(binary)]
    built = subprocess.run(command, capture_output=True, timeout=120)
    (output / 'compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(binary)], capture_output=True, timeout=120)
    (output / 'actual.trace').write_bytes(ran.stdout)
    (output / 'run.log').write_bytes(ran.stderr)
    if ran.returncode:
        raise RuntimeError(ran.stderr.decode())
    expected = gzip.decompress(golden.read_bytes())
    assert ran.stdout == expected, 'Owned TXT stream differs from unchanged frozen oracle'
    (output / 'results.json').write_text(json.dumps(dict(
        command=command, returncode=ran.returncode, cases=expected.count(b'case '),
        trace_bytes=len(expected), trace_sha256=hashlib.sha256(expected).hexdigest(),
        source_sha256=hashlib.sha256(source.encode()).hexdigest(),
        oracle_sha256=hashlib.sha256(oracle.read_bytes()).hexdigest(),
        golden_sha256=hashlib.sha256(golden.read_bytes()).hexdigest(),
        archive_sha256=hashlib.sha256(Path(next(arg for arg in command if arg.endswith('/libxp_ffi.a'))).read_bytes()).hexdigest(),
    ), indent=2) + '\n')
    print(f'Owned TXT stream: {expected.count(b"case ")} frozen cases, {len(expected)} exact trace bytes, balanced files and buffers.')
    fault_main = output / 'fault-main.cpp'
    fault_main.write_text('int bookTxtFaults(); int main() { return bookTxtFaults(); }\n')
    fault_command = [str(fault_main) if arg == str(oracle) else str(output / 'faults') if arg == str(binary) else arg for arg in command]
    built = subprocess.run(fault_command, capture_output=True, timeout=120)
    (output / 'fault-compile.log').write_bytes(built.stdout + built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr.decode())
    ran = subprocess.run([str(output / 'faults')], capture_output=True, timeout=120)
    (output / 'fault-run.log').write_bytes(ran.stdout + ran.stderr)
    if ran.returncode:
        raise RuntimeError(ran.stderr.decode())
    (output / 'fault-results.json').write_text(json.dumps(dict(command=fault_command, returncode=ran.returncode, native_allocation_cases=3, rust_allocation_cases=4), indent=2) + '\n')
    validation(output, native, app)
    build_cache(output, native, app)


if __name__ == '__main__':
    main()
