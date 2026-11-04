"""Install observer calls only in a separate host Rust workspace and C++ recorder."""
from pathlib import Path
import hashlib
import json
import os
import sys

READER = Path('crates/xp-ffi/src/app/epub/section_owner')


def replace_once(source, anchor, value):
    if source.count(anchor) != 1:
        raise ValueError(f'Reader observer anchor must occur once: {anchor!r}')
    return source.replace(anchor, value)


def instrument(relative, source):
    if relative == READER / 'services.rs':
        source = replace_once(source, '    fn trace(&self, _record: &Trace) {}',
                              '    fn trace(&self, record: &Trace) { unsafe { super::observer::semantic(record) }; }')
        source = replace_once(source, '    fn trace_bytes(&self, _bytes: &[u8]) {}',
                              '    fn trace_bytes(&self, bytes: &[u8]) { unsafe { super::observer::bytes(bytes.as_ptr(), bytes.len()) }; }')
    elif relative == READER / 'driver.rs':
        for anchor, call in (
            ('    pub(super) fn begin(&mut self, options: &Options) -> bool {',
             'unsafe { super::observer::begin(options) };'),
            ('    pub(super) fn step(&mut self) -> Step {',
             'unsafe { super::observer::step(self.services().input.consumed()) };'),
            ('    pub(super) fn finish(&mut self) -> bool {',
             'unsafe { super::observer::finish(self.services().stopped.get(), self.oom()) };'),
        ):
            source = replace_once(source, anchor, anchor + '\n        ' + call)
    elif relative == READER / 'mod.rs':
        source = replace_once(source, 'mod driver;', 'mod driver;\nmod observer;')
    return source


OBSERVER = '''// Generated host-only observer imports, absent from the shipping workspace.
use crate::chapter::Options;
use xp_core::chapter::services::Trace;
unsafe extern "C" {
    #[link_name = "reader_section_observe_semantic"] pub(super) fn semantic(record: *const Trace);
    #[link_name = "reader_section_observe_bytes"] pub(super) fn bytes(data: *const u8, len: usize);
    #[link_name = "reader_section_observe_begin"] pub(super) fn begin(options: *const Options);
    #[link_name = "reader_section_observe_step"] pub(super) fn step(position: u32);
    #[link_name = "reader_section_observe_finish"] pub(super) fn finish(stopped: bool, oom: bool);
}
'''


def recorder(repo):
    sys.path.insert(0, str(repo / 'test/reader_lifecycle'))
    sys.path.insert(0, str(repo / 'test/section_golden'))
    from test_popup_resource import definition
    from instrument_chapter import instrument_assembly
    native = (repo / 'lib/Epub/Epub/parsers/ChapterPlatform.cpp').read_text()
    traced = instrument_assembly(native)
    semantic = definition(traced, '[](void*, const XpChapterTrace* record)')
    block = definition(native, 'BlockStyle chapterBlock(const Wire& value)')
    return '''// Generated from the existing native trace formatter; observations only.
#include "ChapterTrace.h"
#include "LayoutTrace.h"
#include <xp_ffi.h>
namespace {
template <typename Wire>
''' + block + '''
}
extern "C" void reader_section_observe_semantic(const XpChapterTrace* record) {
  (''' + semantic + ''')(nullptr, record);
}
extern "C" void reader_section_observe_bytes(const uint8_t* data, size_t len) {
  ChapterTrace::stream.write(reinterpret_cast<const char*>(data), len);
}
extern "C" void reader_section_observe_begin(const XpChapterOptions* options) {
  const auto& a = options->assembly;
  ChapterTrace::record("Begin", a.font, a.compression, a.extra_spacing, a.indent_spaces,
    a.character_spacing, a.word_spacing, options->alignment, a.width, a.height,
    a.hyphenation, a.focus, options->embedded, options->images);
}
extern "C" void reader_section_observe_step(uint32_t position) { ChapterTrace::record("Step", position); }
extern "C" void reader_section_observe_finish(bool stopped, bool oom) { ChapterTrace::record("Finish", stopped, oom); }
'''


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def main():
    repo, destination = (Path(arg).resolve() for arg in sys.argv[1:])
    if destination == repo or destination.is_relative_to(repo / 'rust'):
        raise ValueError('Observer workspace must be separate from shipping Rust')
    destination.mkdir(parents=True, exist_ok=True)
    manifest = destination / 'sources.json'
    previous_manifest = json.loads(manifest.read_text()) if manifest.exists() else {}
    previous = previous_manifest.get('files', {})
    files = {}
    for directory, folders, names in os.walk(repo / 'rust'):
        folders[:] = [name for name in folders if name not in ('target', '.git')]
        for name in names:
            source = Path(directory) / name
            relative = source.relative_to(repo / 'rust')
            raw = source.read_bytes()
            data = instrument(relative, raw.decode()).encode() if relative in (
                READER / 'services.rs', READER / 'driver.rs', READER / 'mod.rs') else raw
            write_changed(destination / 'rust' / relative, data)
            files[str(relative)] = {'source_sha256': hashlib.sha256(raw).hexdigest(),
                                    'observer_sha256': hashlib.sha256(data).hexdigest()}
    for removed in previous.keys() - files.keys():
        (destination / 'rust' / removed).unlink(missing_ok=True)
    write_changed(destination / 'rust' / READER / 'observer.rs', OBSERVER.encode())
    # Cargo's existing i18n build script resolves these outside the Rust workspace.
    inputs = {repo / 'scripts/gen_i18n.py', *(repo / 'lib/I18n/translations').rglob('*')}
    # Preserve the generator's source scan, including XP_I18N_STRIP_UNUSED builds.
    for directory in ('src', 'lib'):
        inputs.update(p for p in (repo / directory).rglob('*') if p.suffix in ('.cpp', '.h', '.c', '.rs'))
    build_inputs = {}
    for source in sorted(inputs):
        if source.is_file():
            data = source.read_bytes()
            relative = str(source.relative_to(repo))
            write_changed(destination / relative, data)
            build_inputs[relative] = hashlib.sha256(data).hexdigest()
    for removed in previous_manifest.get('build_inputs', {}).keys() - build_inputs.keys():
        (destination / removed).unlink(missing_ok=True)
    cpp = recorder(repo).encode()
    write_changed(destination / 'ReaderSectionObserver.cpp', cpp)
    observer = {'rust_sha256': hashlib.sha256(OBSERVER.encode()).hexdigest(),
                'cpp_sha256': hashlib.sha256(cpp).hexdigest(),
                'generator_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                'native_formatter_sha256': hashlib.sha256((repo / 'test/section_golden/instrument_chapter.py').read_bytes()).hexdigest()}
    write_changed(manifest, (json.dumps({'files': files, 'build_inputs': build_inputs,
                                      'observer': observer}, indent=2) + '\n').encode())


if __name__ == '__main__':
    main()
