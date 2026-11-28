"""Compile the reusable contract against source fixtures, not substitute copies."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Header adapters use the fixture's own open mechanism and fault controls.
HEADERS = {
    'book_stdio': ('book_io/epub-stubs/HalStorage.h', 'f.open(path, ok ? "w+b" : "rb");'),
    'chapter_stdio': ('chapter_html_slim_parser/stubs/HalStorage.h', 'f.open(path, ok ? "w+b" : "rb");'),
    'section_stdio': ('section_io/stubs/HalStorage.h', 'f.open(path, ok ? "w+b" : "rb");'),
    'page_stdio': ('page_golden/stubs/HalStorage.h', 'f.open(path, ok ? "w+b" : "rb");'),
    'diagnostic_stdio': ('page_diagnostics/file_stubs/HalStorage.h', 'f.open(path, ok ? "w+b" : "rb");'),
    'css_stdio': ('css_parser/stubs/HalStorage.h', 'f.open(path, ok ? "w+b" : "rb");'),
    'text_stdio': ('text_golden/stubs/HalStorage.h', 'if(ok) { auto* out = fopen(path,"wb"); assert(out); fclose(out); } f.open(path);'),
    'book_memory': ('book_io/stubs/HalStorage.h', 'f.open(path, ok);'),
    'library': ('library_builder/stubs/HalStorage.h', 'if(ok) fake::add(path, "bytes"); f = Storage.open(path);'),
    'dictionary': ('dictionary_engine/stubs/HalStorage.h', 'if(ok) files[path] = Node{}; f = Storage.open(path, 0);'),
    'font_registry': ('font_registry/stubs/HalStorage.h', 'f = Storage.open(ok ? "/.fonts" : path);'),
    'font_system': ('sdfont_owner/system-stubs/HalStorage.h', 'f = Storage.open(ok ? "/.fonts" : path);'),
    'reader_lifecycle': ('reader_lifecycle/stubs/HalStorage.h', 'storageTrace.openOk = ok; Storage.openFileForWrite("TEST",path,f);'),
    'page_transport': ('page_codec_diff/stubs/HalStorage.h', 'if(ok) { static std::vector<uint8_t> bytes; f = HalFile(bytes); } else Storage.openFileForRead("TEST",path,f);'),
    'diagnostic_transport': ('page_diagnostics/stubs/HalStorage.h', 'if(ok) { static std::vector<uint8_t> bytes; f = HalFile(bytes); } else Storage.openFileForRead("TEST",path,f);'),
    'sd_font': ('sd_card_font/stubs/HalStorage.h', 'Storage.openFileForRead("TEST",path,f);'),
    'sd_pool': ('sdfont_pool_diff/stubs/HalStorage.h', 'sdIoFailAt = ok ? 0 : sdIoAttempts + 1; Storage.openFileForRead("TEST",path,f);'),
    'xtc': ('xtc_diff/stubs/HalStorage.h', 'Storage.openFileForRead("TEST",path,f);'),
    'xtc_diagnostic': ('h6_diagnostics/xtc-stubs/HalStorage.h', 'Storage.openFileForRead("TEST",path,f);'),
    'inflate': ('inflate_small/stubs/HalStorage.h', 'if(ok) f.markOpen(); else Storage.openFileForRead("TEST",path,f);'),
    'settings': ('settings_store/stubs/HalStorage.h', 'f.open();'),
    'sync': ('sync_golden/stubs/HalStorage.h', 'fake::openOk = ok; Storage.openFileForRead("TEST",path,f);'),
    'zip': ('zip_file/stubs/HalStorage.h', 'if(ok) Storage.setFile(path,{1,2}); Storage.openFileForRead("TEST",path,f);'),
    'index': ('library_index_file/stubs/HalStorage.h', 'if(ok) Storage.setFile(path,{1,2}); Storage.openFileForRead("TEST",path,f);'),
    'opf': ('content_opf_parser/stubs/Epub.h', 'Storage.openFileForRead("TEST",path,f);'),
    'plugin': ('plugin_policy/stubs/HalStorage.h', 'oracle::fault = ok ? "" : "open"; if(ok) oracle::files[path] = {}; f.open(path);'),
    'web_jobs': ('web_jobs/stubs/HalStorage.h', 'oracle::fault = ok ? "" : "open"; if(ok) oracle::files[path] = {}; f.open(path, false);'),
    'web_server': ('web_server/Host.h', 'if(ok) host::files[path] = std::make_shared<host::Node>(); f = Storage.open(path);'),
}
NO_FAILURE = {'sd_font', 'xtc', 'xtc_diagnostic', 'settings', 'opf'}
BOUNDED = {'book_stdio', 'chapter_stdio', 'section_stdio', 'page_stdio'}

# Local classes are extracted verbatim so their own destructor/move/guard code is tested.
# The prelude supplies only surrounding trace counters and scripted I/O dependencies.
LOCALS = {
 'local_book_open': ('reader_lifecycle/book_open_oracle.cpp', 'int files=0; struct FsFile {};',
     'f = HalFile(std::make_unique<HalFile::Impl>(FsFile{}));'),
 'local_book_resource': ('reader_lifecycle/book_resource_oracle.cpp',
     'int depth=0, closes=0; bool failSeek=false, failRead=false, shortRead=false;',
     'f.hasImpl = true; f.open = ok;'),
 'local_book_scratch': ('reader_lifecycle/book_scratch_oracle.cpp', 'int closes=0;', 'f.hasImpl = true;'),
 'local_book_txt_stream': ('reader_lifecycle/book_txt_stream.cpp',
     'int files=0; void* inputContext=nullptr; int (*inputRead)(void*,uint8_t*,size_t)=nullptr;', 'f.hasImpl = true;'),
 'local_book_txt_validate': ('reader_lifecycle/book_txt_validate.cpp',
     'int liveFiles=0; std::vector<std::string> events; std::map<std::string,std::vector<uint8_t>> files; bool operation(std::string) { return true; }',
     'f.hasImpl = true; if(ok) f.path=path;'),
 'local_book_lifetime': ('reader_lifecycle/book_lifetime_oracle.cpp',
     'struct { int readSize=0; std::vector<uint8_t> bytes; } config; template<class... T> void event(T...) {}',
     'f.hasImpl=true; f.open=ok;'),
 'local_home_cache': ('home_ui/cache_leaves.cpp', '', 'f.hasImpl=true;'),
 'local_home_theme': ('home_ui/theme_leaves.cpp', '', 'f.hasImpl=true;'),
 'local_ui_effects': ('ui_updates/effects.cpp',
     'struct { bool rootDir=true; unsigned size=0; } cfg; struct Entry { const char* name; bool dir; }; std::vector<Entry> entries; template<class... T> void record(T...) {}',
     'f.hasImpl=true; f.valid=ok; f.root=true;'),
}

def local_class(path):
    text = (ROOT / 'test' / path).read_text()
    start = text.index('struct HalFile')
    end = text.index('\n};', start) + 3
    return text[start:end]


def generate(out):
    out.mkdir(parents=True, exist_ok=True)
    cmake = []
    cases = dict(HEADERS)
    cases['clipping'] = ('clipping_bytes/stubs/HalStorage.h', 'if(ok) clip_test::nodes[path] = std::make_shared<clip_test::Node>(); f = PhysicalVolume{}.open(path, O_RDONLY);')
    cases.update({name: (path, opening) for name, (path, _, opening) in LOCALS.items()})
    for wrapper in ['writer', 'package', 'zip', 'css']:
        cases['wrapper_' + wrapper] = ('reader_lifecycle/book_' + wrapper + '_oracle.cpp', 'f.open(path, ok ? "w+b" : "rb");')
    for name, (header, opening) in cases.items():
        includes = [str(ROOT / 'test' / Path(header).parent), str(ROOT / 'test/section_io/stubs'), str(ROOT / 'test/book_io/epub-stubs'), str(ROOT / 'test/text_golden/stubs'), str(ROOT / 'test/web_jobs/stubs'), str(ROOT / 'rust/crates/xp-ffi/include'), str(ROOT)]
        source = '#include <cassert>\n#include <initializer_list>\n'
        if name == 'plugin':
            source += '#include "Oracle.h"\n'
        if name == 'clipping':
            original = (ROOT / 'test' / header).read_text()
            first = original.index('// Method bodies in SdkInline.inc')
            last = original.index('inline HalFile PhysicalVolume::open', first)
            source += (original[:first] + original[last:]).replace('../../hal_fake_strict.h', str(ROOT / 'test/hal_fake_strict.h')) + '\n'
        elif name in LOCALS or name.startswith('wrapper_'):
            source += '#include <algorithm>\n#include <cstring>\n#include <map>\n#include <memory>\n#include <string>\n#include <vector>\n'
            source += f'#include "{ROOT / "test/hal_fake_strict.h"}"\n'
            if name in LOCALS:
                source += LOCALS[name][1] + '\n'
                source += '[[noreturn]] void unopenedFile(const char* op) { hal_fake::unopenedHandle(op, ""); }\n'
            else:
                source += '#define HalFile HostFile\n'
                source += f'#include "{ROOT / "test/chapter_html_slim_parser/stubs/HalStorage.h"}"\n#undef HalFile\n'
                source += 'size_t liveFiles=0, born=0, closes=0, closeCalls=0, closeFailures=0; bool failClose=false;\n'
            source += local_class(header) + '\n'
        else:
            source += f'#include "{ROOT / "test" / header}"\n'
        source += f'#include "{ROOT / "test/hal_contract/Contract.h"}"\n'
        source += 'int main() {\n'
        source += '  hal_contract::check<HalFile>([](HalFile& f, const char* path, bool ok) { ' + opening + ' }, ' + str(name not in NO_FAILURE).lower() + ');\n'
        if name in BOUNDED:
            source += f'static_assert(sizeof(HalFile) == {48 if name in {"book_stdio", "section_stdio"} else 40});\n'
            source += '''
  // These goldens record the 64-bit host ABI (FILE*, std::string, optional Print vptr).
  static_assert(sizeof(size_t) == 8, "size-recording stdio fixtures require a 64-bit host");
  HalFile budget;
  assert(budget.open("contract-budget", "w+b"));
  budget.limitWritesTo(UINT32_MAX);
  assert(budget.write(uint8_t{1}) == 1);
  budget.limitWritesTo(1);
  assert(budget.write(uint8_t{1}) == 1);
  assert(budget.write(uint8_t{1}) == 0);
  hal_contract::rejects([&] { budget.limitWritesTo(size_t{UINT32_MAX} + 1); }, nullptr);
  hal_contract::rejects([&] { budget.limitWritesTo(SIZE_MAX); }, nullptr);
  HalFile unlimited;
  assert(unlimited.open("contract-default", "w+b"));
  hal_contract::rejects([&] { unlimited.write(static_cast<const void*>(nullptr), size_t{UINT32_MAX} + 1); }, nullptr);
  unlimited.close();
  budget.close();
'''
        source += '  std::printf("contract passed: ' + name + ' sizeof=%zu\\n", sizeof(HalFile));\n}\n'
        (out / f'{name}.cpp').write_text(source)
        cmake += [f'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "{ROOT / "test" / header}")', f'add_executable(HalContract_{name} "{out / (name + ".cpp")}")',
                  f'target_include_directories(HalContract_{name} PRIVATE ' + ' '.join('"'+p+'"' for p in includes) + ')',
                  f'target_link_libraries(HalContract_{name} PRIVATE GTest::gtest)',
                  f'target_compile_options(HalContract_{name} PRIVATE -UNDEBUG)',
                  f'file(MAKE_DIRECTORY "{out / name}")',
                  f'add_test(NAME HalContract.{name} COMMAND HalContract_{name})',
                  f'set_tests_properties(HalContract.{name} PROPERTIES WORKING_DIRECTORY "{out / name}")']
        if name == 'web_server':
            cmake += [f'target_include_directories(HalContract_{name} PRIVATE ${{XP_TEST_ARDUINOJSON_SRC}})']
    (out / 'targets.cmake').write_text('\n'.join(cmake)+'\n')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    generate(parser.parse_args().output.resolve())
