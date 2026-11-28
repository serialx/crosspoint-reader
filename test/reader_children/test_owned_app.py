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
from extract import definition
from legacy_page_native import source as legacy_page_source
from book_facade_composition import facade_bridge_source, compile_command
from test_popup_resource import replace_once
from test_reader_settings import generated_i18n_directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument("--mode", action="append", help="Run only these actual-App scenarios")
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
        source = replace_once(source, 'void initialize() {',
            'static bool observeOwnedClippingLog(uint8_t, XpAppText, XpAppText);\nvoid initialize() {')
        source = replace_once(source, '      if (text(origin) == "CLIP") {\n        std::fprintf',
            '      if (text(origin) == "CLIP") {\n        if (observeOwnedClippingLog(level, origin, message)) return;\n        std::fprintf')
        owned = (HERE / 'owned_app.cpp.in').read_text()
        refresh = (HERE / "frontlight_refresh.cpp.inc").read_text()
        refresh = refresh.replace("// PRODUCTION_REFRESH_METHODS", "\n".join(
            definition((ROOT / path).read_text(), signature) for path, signature in (
                ("lib/GfxRenderer/GfxRenderer.cpp", "HalDisplay::RefreshMode GfxRenderer::applyPromotedRefresh("),
                ("src/activities/Activity.cpp", "void Activity::finish("),
                ("src/activities/ActivityManager.cpp", "void ActivityManager::popActivity("),
                ("src/activities/util/UtilityActivity.cpp", "void UtilityActivity::applyUtilityEffect("),
                ("src/activities/util/FrontlightPanelActivity.cpp", "void FrontlightPanelActivity::applyUtilityEffect("),
                ("src/activities/util/FrontlightPanelActivity.cpp", "void FrontlightPanelActivity::onExit("),
            )
        ))

        owned = owned.replace('int main(int argc, char** argv)', (HERE / 'owned_clipping.cpp.inc').read_text() + (HERE / 'owned_bookmark.cpp.inc').read_text() + refresh + '\nint main(int argc, char** argv)', 1)
        source += owned
        candidate = out / 'candidate.cpp'
        candidate.write_text(source)
        facade = out / 'facade.cpp'
        facade.write_text(facade_bridge_source(ROOT))
        binary = out / 'candidate'
        command = compile_command(ROOT, out, facade, binary, archive=str(args.archive))
        # Translation IDs must match the linked archive.
        i18n = generated_i18n_directory(args.archive)
        command[command.index('-o'):command.index('-o')] = ['-I', str(i18n), '-I', str(ROOT / 'src/activities'), '-I', str(ROOT / 'src'), str(candidate)]
        allocator = out / 'allocator.cpp'
        allocator.write_text(replace_once((ROOT / 'test/cmake/xp_ffi_host.cpp').read_text(),
            'extern "C" void heap_caps_free(void* ptr) {',
            'extern "C" void observeRustFree(void*);\nextern "C" void heap_caps_free(void* ptr) { observeRustFree(ptr);'))
        command = [str(allocator) if arg == str(ROOT / 'test/cmake/xp_ffi_host.cpp') else arg for arg in command]
        subprocess.run(command, check=True)
        for mode in ('refresh', 'chapter-timed', 'menu', 'chapter', 'percent', 'qr', 'text', 'text-overlay', 'clippings', 'clippings-nav', 'clippings-delete', 'clippings-passage', 'clippings-detail', 'clippings-detail-oom', 'clippings-oom', 'bookmarks', 'bookmarks-empty', 'bookmarks-delete', 'bookmarks-delete-fail', 'bookmarks-oom', 'bookmarks-stack-oom', 'bookmarks-rename', 'bookmarks-rename-empty', 'bookmarks-rename-max', 'bookmarks-record-oom', 'bookmarks-rename-fail', 'bookmarks-swipe', 'bookmarks-swipe-absent', 'bookmarks-swipe-outside', 'bookmarks-touch-hold', 'bookmarks-command-oom', 'bookmarks-return-last', 'bookmarks-return-nonlast', 'bookmarks-return-jump', 'bookmarks-return-save', 'bookmarks-return-success'):
            if args.mode and mode not in args.mode:
                continue
            for cancelled in ('0', '1'):
                for debug in ('0', '1'):
                    result = subprocess.run([str(binary), mode, cancelled, debug], capture_output=True, text=True)
                    if result.returncode:
                        print(result.stderr[-15000:])
                        raise AssertionError((mode, cancelled, debug, result.returncode))
                    print(result.stdout)
        if args.mode:
            return
        # A swipe release has no tap coordinates; substituting them must lose the target row.
        candidate.write_text(replace_once(source,
            '*x = bookmarkPointX; *y = bookmarkPointY; return 1;',
            '*x = childInput.input.touch_x; *y = childInput.input.touch_y; return 1;'))
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary), 'bookmarks-swipe', '0', '0'], capture_output=True, text=True)
        assert result.returncode != 0 and 'remove != bookmarkCommands.end()' in result.stderr
        print('Owned Bookmarks swipe release-coordinate negative control rejected')


if __name__ == '__main__':
    main()
