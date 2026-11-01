"""Compile production facade routes with deterministic platform/resource leaves."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from input_oracle import method

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SOURCE = Path(os.environ.get('XP_REJECTION_SOURCE_ROOT', ROOT))


def extracted(path, signatures):
    source = (SOURCE / path).read_text()
    return '\n'.join(method(source, signature) for signature in signatures)


def run_cpp(source, reject=False):
    archive = Path(os.environ.get('XP_FFI_HOST_ARCHIVE', ROOT / 'rust/target/release/libxp_ffi.a'))
    with tempfile.TemporaryDirectory() as directory:
        out = Path(directory)
        (out / 'probe.cpp').write_text(source)
        subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-fsanitize=address,undefined',
                        '-I' + str(ROOT / 'rust/crates/xp-ffi/include'), str(out / 'probe.cpp'),
                        str(ROOT / 'test/cmake/xp_ffi_host.cpp'), str(archive), '-o', str(out / 'probe')], check=True)
        result = subprocess.run([str(out / 'probe')], capture_output=True, timeout=15)
        if reject:
            assert result.returncode != 0, 'deliberate regression passed'
        elif result.returncode:
            raise AssertionError(result.stderr.decode())


class FacadeRoutes(unittest.TestCase):
    def test_library_resource_lifetime(self):
        source = (HERE / 'facade_library.cpp').read_text()
        header = (ROOT / 'src/activities/library/LibraryListActivity.h').read_text()
        start = header.index('  StaticSemaphore_t resourceMutexStorage')
        end = header.index('  int listCount(', start)
        source = source.replace('// RESOURCE_LOCK', header[start:end])
        source += extracted('src/activities/library/LibraryListActivity.cpp', [
            'void LibraryListActivity::event(', 'void LibraryListActivity::rustPublished()',
            'void LibraryListActivity::applyFilter()',
            'void LibraryListActivity::applyRustEffect(',
            'void LibraryListActivity::openSearch()', 'bool LibraryListActivity::collapseGroups(',
            'void LibraryListActivity::expandGroup(', 'const char* LibraryListActivity::headerTitle()',
            'bool LibraryListActivity::buildRustScene('])
        source += (HERE / 'facade_library_cases.cpp').read_text()
        run_cpp(source)
        run_cpp(source.replace('ResourceLock lock(*this);', 'RenderLock lock(*this);'), reject=True)
        run_cpp(source.replace('ResourceLock resourcesLock(*this);', ''), reject=True)
        run_cpp(source.replace('if (!resourcesChanged)', 'if (true)'), reject=True)
        run_cpp(source.replace('  groupCount = 0;',
                               "  groupCount = 0; std::string(2000, 'x').swap(frame.headerSearchTitle);"),
                reject=True)

    def test_home_validation_does_not_wrap(self):
        subprocess.run(['cargo', 'test', '--offline', '--locked', '--manifest-path',
                        str(HERE / 'app_owner/Cargo.toml'),
                        'app::home::tests::validation_does_not_wrap', '--', '--exact'], check=True)

    def test_popup_open(self):
        source = (HERE / 'facade_file.cpp').read_text()
        popup = (ROOT / 'src/components/OptionPopup.h').read_text()
        methods = '\n'.join(method(popup, signature) for signature in [
            'void show(StrId titleId, const StrId* optionIds',
            'bool handleButtons(', 'void activate('])
        source = source.replace('// POPUP_METHODS', methods)
        source += extracted('src/activities/home/FileBrowserActivity.cpp', [
            'void FileBrowserActivity::activateSelected()', 'void FileBrowserActivity::showEntryActions()',
            'XpUiFileFacts FileBrowserActivity::facts(', 'void FileBrowserActivity::loop()',
            'XpUiInterest FileBrowserActivity::rustInterest(', 'XpUiEffect FileBrowserActivity::updateRust('])
        source += (HERE / 'facade_file_cases.cpp').read_text()
        run_cpp(source)
        run_cpp(source.replace('effect = xp_ui_file_activate(&state, &f);',
                               'XpUiInput input{}; input.released = XP_UI_CONFIRM; '
                               'effect = xp_ui_file_update(&state, &input, &f).effect;'), reject=True)
        fresh = source.replace('const auto f = facts(state.list.selected);',
                               'auto f = facts(state.list.selected); f.long_confirm = f.long_touch = 0;')
        run_cpp(fresh.replace('effect = xp_ui_file_activate(&state, &f);',
                              'XpUiInput input{}; input.released = XP_UI_CONFIRM; '
                              'effect = xp_ui_file_update(&state, &input, &f).effect;'), reject=True)


if __name__ == '__main__':
    unittest.main()
