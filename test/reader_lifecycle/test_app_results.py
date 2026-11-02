"""Actual App child destruction, publication, unlock and borrowed EPUB results.

Uses physical storage/UI fakes and checks ordering with actual-source mutants.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

MODES = ('menu', 'chapter', 'percent', 'text', 'text-overlay', 'footnote',
         'footnote-page', 'clip', 'lookup', 'bookmark', 'dictionary',
         'bookmarks', 'clippings', 'qr', 'wifi')
WIFI_MATRIX = ('wifi-success', 'wifi-reopen-failure', 'wifi-disconnected', 'wifi-cancelled')


def block(source, signature):
    """Extract an existing C++ brace body, retaining its original spelling."""
    assert source.count(signature) == 1, signature
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(('unclosed body', signature))


def make_source(root, here):
    base_path = root / 'test/reader_lifecycle/app_owned_epub.cpp'
    base = base_path.read_text()
    assert base.count('int main(') == 1
    base = base.split('int main(', 1)[0]
    trace = 'events.push_back({name, generation, id, renderDepth, modelDepth});'
    assert base.count(trace) == 1
    base = base.replace(trace, trace + '\n    std::fprintf(stderr, "physical %s generation=%u id=%u render=%d model=%d\\n", name.c_str(), generation, id, renderDepth, modelDepth);')
    manager = (root / 'src/activities/ActivityManager.cpp').read_text()
    owner = (root / 'src/activities/OwnedLegacyActivity.h').read_text()
    extension = (here / 'app_results_main.inc').read_text()
    substitutions = {
        '// OWNED_NATIVE_DECLARATION': block(owner, 'struct OwnedLegacyActivity') + ';',
        '// NATIVE_ACTIVITY_GETTER': block(owner, 'Activity* nativeActivity(const void* object)'),
        '// NATIVE_WITH_RESULT': block(manager, '[](void* activity, XpAppResultSink sink'),
        '// PHYSICAL_BINDINGS': (here / 'app_results_physical.inc').read_text(),
    }
    for marker, value in substitutions.items():
        assert extension.count(marker) == 1, marker
        extension = extension.replace(marker, value)
    return base + '\n' + extension


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_APP_RESULTS_EVIDENCE_DIR'))
    parser.add_argument('--mutants', action='store_true')
    parser.add_argument('--case', choices=(*MODES, *WIFI_MATRIX, 'stack-failure', 'pending-shutdown'))
    parser.add_argument('--cancelled', choices=(0, 1), type=int)
    args = parser.parse_args()
    root = args.repo.resolve()
    here = Path(__file__).resolve().parent
    temporary_evidence = tempfile.TemporaryDirectory(prefix='app-results-evidence-') if args.evidence_dir is None else None
    evidence = args.evidence_dir.resolve() if args.evidence_dir else Path(temporary_evidence.name) / 'evidence'
    evidence.mkdir(parents=True, exist_ok=False)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(root / 'test'))
    from rust_host import rust_link_flags
    cases = [(mode, flag) for mode in MODES for flag in (0, 1)]
    cases += [('stack-failure', 0), ('pending-shutdown', 0)]
    cases += [(mode, 0) for mode in WIFI_MATRIX]
    if args.case:
        cases = [case for case in cases if case[0] == args.case]
    if args.cancelled is not None:
        cases = [case for case in cases if case[1] == args.cancelled]
    assert cases
    sources = [Path(__file__), here / 'app_results_main.inc', here / 'app_results_physical.inc',
               root / 'test/reader_lifecycle/app_owned_epub.cpp',
               root / 'test/reader_lifecycle/physical_defaults.h',
               root / 'test/reader_lifecycle/scene_physical_bindings.inc',
               root / 'src/activities/ActivityManager.cpp',
               root / 'src/activities/OwnedLegacyActivity.h',
               root / 'src/activities/ActivityResult.h',
               root / 'src/activities/ActivityResultView.h',
               args.archive.resolve()]
    sources += list((root / 'rust/crates/xp-ffi/src/app').rglob('*.rs'))
    sources += list((root / 'rust/crates/xp-ui/src/app').rglob('*.rs'))
    hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
    (evidence / 'source-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
    commands, records = [], []
    def run(command, label, expected=0, marker=None):
        commands.append([str(value) for value in command])
        (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        (evidence / (label + '.stdout')).write_bytes(result.stdout)
        (evidence / (label + '.stderr')).write_bytes(result.stderr)
        records.append({'label': label, 'returncode': result.returncode,
                        'stdout_sha256': hashlib.sha256(result.stdout).hexdigest(),
                        'stderr_sha256': hashlib.sha256(result.stderr).hexdigest()})
        assert result.returncode == expected, (label, result.returncode, result.stderr.decode(errors='replace'))
        if marker: assert result.stderr.splitlines()[-1] == ('EXPECTED_ASSERTION: ' + marker).encode(), (label, result.stderr)
    try:
        with tempfile.TemporaryDirectory(prefix='epub-app-results-') as directory:
            work = Path(directory)
            source = evidence / 'candidate.cpp'
            source.write_text(make_source(root, here))
            executable = work / 'app-results'
            compile_args = ['c++', '-std=c++20', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                 '-fno-sanitize-recover=all', '-DXP_FFI_HOST_FAULT_INJECTION',
                 '-I', str(root / 'test/reader_lifecycle'), '-I', str(root / 'test/ui_runtime'),
                 '-I', str(root / 'src/activities'), '-I', str(root / 'rust/crates/xp-ffi/include'),
                 str(source)]
            run([*compile_args, *rust_link_flags(archive=str(args.archive.resolve())), '-o', str(executable)], 'compile')
            for mode, cancelled in cases:
                run([str(executable), mode, str(cancelled)], f'{mode}-{cancelled}')
            if args.mutants:
                copied = work / 'copied'
                shutil.copytree(root / 'rust', copied / 'rust', ignore=shutil.ignore_patterns('target', '__pycache__'))
                for name in ('scripts', 'lib', 'src'):
                    (copied / name).symlink_to(root / name, target_is_directory=True)
                ui = Path('crates/xp-ui/src/app/mod.rs')
                child = Path('crates/xp-ffi/src/app/epub/child_result.rs')
                owner = Path('crates/xp-ffi/src/app/epub/ownership.rs')
                recovery = Path('crates/xp-ffi/src/app/epub/recovery.rs')
                def once(original, before, after):
                    assert original.count(before) == 1, before
                    return original.replace(before, after, 1)
                ui_source = (root / 'rust' / ui).read_text()
                early = once(ui_source, '            drop(guard);\n            if let Some(parent) = parent {\n                services.deliver_result(parent, result);',
                    '            if let Some(parent) = parent { services.deliver_result(parent, result); }\n            drop(guard);\n            if let Some(_parent) = parent {')
                mutations = [
                    ('delivery-before-unlock', ui, early, 'menu', 'APP_RESULT_BEFORE_UNLOCK'),
                    ('no-parent-publication', ui, once(ui_source, '            services.publish(parent, visit);', '            let _ = visit;'), 'menu', 'APP_RESULT_NOT_PUBLISHED'),
                    ('retained-result-intent', child, once((root / 'rust' / child).read_text(), 'core::mem::take(&mut state.child)', 'state.child'), 'menu', 'APP_RESULT_INTENT_RETAINED'),
                    ('retained-stack-intent', owner, once((root / 'rust' / owner).read_text(), '        self.model(|state| state.child = super::state::ChildIntent::None);', ''), 'stack-failure', 'APP_STACK_INTENT_RETAINED'),
                    ('wrong-reopen-destination', recovery, once((root / 'rust' / recovery).read_text(),
                        'path: self.path.wire(),\n                fast_initial: 0,',
                        'path: app_abi::Text::from_bytes(b"/wrong.epub"),\n                fast_initial: 0,'),
                        'wifi-success', 'WIFI_REOPEN_DESTINATION'),
                ]
                for name, path, content, case, marker in mutations:
                    original = (copied / 'rust' / path).read_text()
                    (copied / 'rust' / path).write_text(content)
                    try:
                        (evidence / (name + '.rs')).write_text(content)
                        run(['cargo', 'build', '--manifest-path', str(copied / 'rust/Cargo.toml'),
                             '--target-dir', str(work / 'mutant-target'), '-p', 'xp-ffi', '--release',
                             '--locked', '--features', 'app-test'], name + '.cargo')
                        archive = work / 'mutant-target/release/libxp_ffi.a'
                        run([*compile_args, *rust_link_flags(archive=str(archive)), '-o', str(executable)], name + '.compile')
                        run([str(executable), case, '0'], name, 42, marker)
                    finally:
                        (copied / 'rust' / path).write_text(original)
        summary = {'case_count': len(cases), 'full_matrix_count': 36,
                   'mutant_count': 5 if args.mutants else 0,
                   'policy_hooks': 0, 'results': records,
                   'reopen': 'actual Wi-Fi child return, returning restart leaf and queued Reader replacement; null acquisition preserves owner',
                   'wifi_limits': 'physical Wi-Fi/restart/reader factory fakes; shipping App still uses legacy Reader creation until atomic routing',
                   'scope': 'physical no-hook App destruction/publication/unlock/delivery; see MAPPING.md limits'}
        (evidence / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
        print(f'App EPUB results: {len(cases)} cases, {summary["mutant_count"]} semantic mutants passed')
    finally:
        (evidence / 'attempts.json').write_text(json.dumps(records, indent=2) + '\n')
        assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in hashes.items())
        if temporary_evidence: temporary_evidence.cleanup()


if __name__ == '__main__':
    main()
