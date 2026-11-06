"""Exercise owned reader children through real App, ReaderBook and EpubSession."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'test/reader_lifecycle'))
from test_child_handle_composition import fixture, NATIVE, OWNER, MANAGER
from oracle_sources import native_text
from legacy_page_native import source as legacy_page_source
from book_facade_composition import facade_bridge_source, compile_command
from test_popup_resource import replace_once


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='reader-owned-app-') as temporary:
        out = Path(temporary)
        source = fixture(legacy_page_source(native_text(NATIVE)), OWNER.read_text(), MANAGER.read_text())
        source = source.split('int main(int argc, char** argv)', 1)[0]
        source = replace_once(source, 'std::vector<uint8_t> readerCacheBytes() {',
            'static bool longChapterFixture;\nstd::vector<uint8_t> readerCacheBytes() {')
        source = replace_once(source, 'number(row, sizes[i], 4); number(row, 0, 2);',
            'number(row, sizes[i], 4); number(row, longChapterFixture ? 31 : 0, 2);')
        source = replace_once(source, 'for (unsigned i = 0; i < 5; ++i) {',
            'for (unsigned i = 0; i < (longChapterFixture ? 1000 : 5); ++i) {')
        source = replace_once(source, 'stringBytes(row, "Chapter");',
            'stringBytes(row, longChapterFixture ? std::to_string(i) + std::string(220, \'x\') : "Chapter");')
        source = replace_once(source, 'number(bytes, 7, 2); number(bytes, 5, 2);',
            'number(bytes, 7, 2); number(bytes, longChapterFixture ? 1000 : 5, 2);')
        source = replace_once(source, 'view->toc_count == 5',
            'view->toc_count == (longChapterFixture ? 1000 : 5)')
        source += (HERE / 'owned_app.cpp.in').read_text()
        candidate = out / 'candidate.cpp'
        candidate.write_text(source)
        facade = out / 'facade.cpp'
        facade.write_text(facade_bridge_source(ROOT))
        binary = out / 'candidate'
        command = compile_command(ROOT, out, facade, binary, archive=str(args.archive))
        command[command.index('-o'):command.index('-o')] = ['-I', str(ROOT / 'src/activities'), '-I', str(ROOT / 'src'), str(candidate)]
        allocator = out / 'allocator.cpp'
        allocator.write_text(replace_once((ROOT / 'test/cmake/xp_ffi_host.cpp').read_text(),
            'extern "C" void heap_caps_free(void* ptr) {',
            'extern "C" void observeRustFree(void*);\nextern "C" void heap_caps_free(void* ptr) { observeRustFree(ptr);'))
        command = [str(allocator) if arg == str(ROOT / 'test/cmake/xp_ffi_host.cpp') else arg for arg in command]
        subprocess.run(command, check=True)
        for mode in ('chapter-timed', 'menu', 'chapter', 'percent', 'qr', 'text', 'text-overlay'):
            for cancelled in ('0', '1'):
                for debug in ('0', '1'):
                    result = subprocess.run([str(binary), mode, cancelled, debug], capture_output=True, text=True)
                    if result.returncode:
                        print(result.stderr[-15000:])
                        raise AssertionError((mode, cancelled, debug, result.returncode))
                    print(result.stdout)


if __name__ == '__main__':
    main()
