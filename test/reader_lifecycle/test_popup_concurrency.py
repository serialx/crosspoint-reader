"""Run actual owner popup calls and OptionPopup content under a non-recursive mutex."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from test_popup_resource import fixture, replace_once
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tsan', action='store_true', help='run the native widget baseline with ThreadSanitizer')
    args = parser.parse_args()
    prefix = (HERE / 'popup_resource_oracle.cpp').read_text().split('static XpAppText wire(')[0]
    prefix = replace_once(prefix, '  renderedCount = props.optionCount;',
                          '  paintPause();\n  renderedCount = props.optionCount;')
    popup = (ROOT / 'src/components/OptionPopup.h').read_text()
    native = (ROOT / 'src/activities/reader/RustEpubResources.cpp').read_text()
    combined = ('#include <chrono>\n#include <future>\n#include <mutex>\n'
                '#include "render_entry_gate_fixture.h"\nstatic void paintPause();\n' +
                fixture(popup, native, prefix) + (HERE / 'popup_concurrency.cpp').read_text())
    mutations = (
        ('failure-show-unlocked', Path('crates/xp-ffi/src/app/epub/recovery.rs'),
         'let access = self.lock_resources();\n            self.show_failure_choices(&access, message, &labels[..',
         'let access = self.borrowed_resources();\n            self.show_failure_choices(&access, message, &labels[..',
         'renderDepth == 1 && modelDepth == 0'),
        ('text-show-unlocked', Path('crates/xp-ffi/src/app/epub/settings.rs'),
         "fn text_popup_access(&self) -> Option<ResourceAccess<'_>> {\n        let access = self.lock_resources();",
         "fn text_popup_access(&self) -> Option<ResourceAccess<'_>> {\n        let access = unsafe { self.borrowed_resources() };",
         'renderDepth == 1 && modelDepth == 0'),
        ('text-late-rejection-ignored', Path('crates/xp-ffi/src/app/epub/settings.rs'),
         'if self.model(|state| state.scene_gate.failed) {',
         'if false && self.model(|state| state.scene_gate.failed) {',
         'late rejection must preserve popup content'),
        ('text-intent-published-before-lock', Path('crates/xp-ffi/src/app/epub/settings.rs'),
         'let Some(access) = self.text_popup_access() else {\n                    return;\n                };\n                self.model(|state| state.popup = PopupIntent::Spacing);',
         'self.model(|state| state.popup = PopupIntent::Spacing);\n                let Some(access) = self.text_popup_access() else {\n                    return;\n                };',
         'index == 0'),
    )
    with tempfile.TemporaryDirectory(prefix='reader-popup-concurrency-') as directory:
        work = Path(directory)
        source = work / 'oracle.cpp'
        source.write_text(combined)

        def compile_fixture(name, archive=None):
            binary = work / name
            command = ['c++', '-std=c++20', '-UNDEBUG', '-pthread', '-g',
                       '-fsanitize=thread' if args.tsan else '-fsanitize=address,undefined',
                       '-fno-sanitize-recover=all', '-I', str(HERE), str(source),
                       *rust_link_flags(archive=archive), '-o', str(binary)]
            subprocess.run(command, check=True)
            return binary

        subprocess.run([str(compile_fixture('baseline'))], check=True, timeout=30)
        print(json.dumps({'check': 'thread-sanitizer' if args.tsan else 'address-undefined-sanitizer',
                          'popup_sha256': hashlib.sha256(popup.encode()).hexdigest(),
                          'fixture_sha256': hashlib.sha256(combined.encode()).hexdigest(),
                          'policy_hooks': [],
                          'limitation': 'TSan instruments C++ widget/fakes; linked Rust is not TSan-instrumented'}), flush=True)
        if args.tsan:
            return
        copied_repo = work / 'source'
        copied_rust = copied_repo / 'rust'
        shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
        for name in ('scripts', 'lib', 'src'):
            (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
        target = work / 'target'
        for name, path, before, after, failure in mutations:
            original = (ROOT / 'rust' / path).read_text()
            changed = replace_once(original, before, after)
            (copied_rust / path).write_text(changed)
            subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                            '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                            '--features', 'app-test'], cwd=copied_rust, check=True)
            failed = subprocess.run([str(compile_fixture(name, str(target / 'release/libxp_ffi.a')))],
                                    capture_output=True, text=True, timeout=30)
            assert failed.returncode != 0 and failure in failed.stderr, failed.stderr
            print(json.dumps({'mutant': name, 'returncode': failed.returncode, 'source': str(path),
                              'replacement_count': 1, 'source_sha256': hashlib.sha256(original.encode()).hexdigest(),
                              'mutant_sha256': hashlib.sha256(changed.encode()).hexdigest()}), flush=True)
            (copied_rust / path).write_text(original)
            assert (ROOT / 'rust' / path).read_text() == original


if __name__ == '__main__':
    main()
