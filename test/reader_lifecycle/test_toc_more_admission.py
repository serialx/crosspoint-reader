"""Verify TOC/More rejection at each resource acquisition.

Requires an app-test archive without reader-strips. Deterministic physical lock injection,
with existing historical hooks, is not a real-thread race or full A1 proof.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
PRODUCTION = {
    'toc': Path('crates/xp-ffi/src/app/epub/overlay_input.rs'),
    'more': Path('crates/xp-ffi/src/app/epub/menu_more.rs'),
}
LIMITS = ('Physical deterministic ResourceAccess admission injection, not a real-thread race or full A1. '
          'TOC uses the existing overlay-input hooks; More uses the existing menu hooks, including '
          'settle/discard, percent calculation, bookmark reload and result consumers. The actual '
          'activate_contents_row/activate_more_row methods and child intent dispatch run in Rust. '
          'Only non-strip host configuration is covered here; immutable oracle comparisons remain separate.')

PREAMBLE = r'''
#include <cstring>
#include <sstream>
#include <stdexcept>
extern "C" void xp_epub_test_overlay_reject(void*);
struct AdmissionEvent { std::string name; unsigned depth; int a = 0, b = 0; };
static std::vector<AdmissionEvent> admissionEvents;
static unsigned admissionAcquisitions = 0, admissionRejectAt = 0;
static unsigned countEvent(const char* name) {
  return std::count_if(admissionEvents.begin(), admissionEvents.end(),
                       [name](const AdmissionEvent& e) { return e.name == name; });
}
static bool hasEventValue(const char* name, int value) {
  return std::any_of(admissionEvents.begin(), admissionEvents.end(),
                     [=](const AdmissionEvent& e) { return e.name == name && e.a == value; });
}
static void requireAdmission(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
'''

COMMON = r'''
static bool sameOverlay(const XpEpubTestOverlayObservation& a, const XpEpubTestOverlayObservation& b) {
  return a.focus == b.focus && a.index == b.index && a.counter == b.counter && a.overlay == b.overlay &&
         a.cursor == b.cursor && a.hold == b.hold && a.stored == b.stored && a.toolbar == b.toolbar &&
         a.failed == b.failed;
}
'''


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('fixture transform anchor', before, source.count(before))
    return source.replace(before, after, 1)


def import_adapter(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def toc_fixture(adapter):
    source, old_main = adapter.transformed_fixture().split('int main() {', 1)
    assert old_main
    source = replace_once(source, 'static unsigned eventIndex = 0, lockDepth = 0;',
                          PREAMBLE + '\nstatic unsigned eventIndex = 0, lockDepth = 0;')
    source = replace_once(source, '  std::printf("%u %s %d %d lock=%u\\n", eventIndex++, name, a, b, lockDepth);',
                          '  admissionEvents.push_back({name, lockDepth, a, b});\n'
                          '  std::printf("%u %s %d %d lock=%u\\n", eventIndex++, name, a, b, lockDepth);')
    source = replace_once(source, 'assert(!base.failed && bool(base.toolbar) == bool(owner.toolbarUi));',
                          'assert(bool(base.toolbar) == bool(owner.toolbarUi));')
    source = replace_once(source, 'overlayInputOwner(); assert(!lockDepth); return new RenderLock;',
                          'overlayInputOwner(); assert(!lockDepth);\n'
                          '    auto* token = new RenderLock;\n'
                          '    ++admissionAcquisitions;\n'
                          '    if (admissionAcquisitions == admissionRejectAt) {\n'
                          '      assert(activeOverlayInput->rust && lockDepth == 1 && !overlayInputModelDepth);\n'
                          '      xp_epub_test_overlay_reject(activeOverlayInput->rust);\n'
                          '    }\n'
                          '    return token;')
    return source + COMMON + (HERE / 'toc_admission_main.inc').read_text()


def more_fixture(adapter, root):
    source = adapter.transformed_fixture((root / 'test/reader_lifecycle/menu_parent_oracle.cpp').read_text())
    source, old_main = source.split('int main() {', 1)
    assert old_main
    source = replace_once(source, 'static int lockDepth = 0;', '')
    original_event = 'static void event(const char* name, int a = 0, int b = 0) { std::printf("%s %d %d\\n", name, a, b); }'
    source = replace_once(source, original_event,
                          PREAMBLE + '\nstatic int lockDepth = 0;\n'
                          'static void event(const char* name, int a = 0, int b = 0) {\n'
                          '  admissionEvents.push_back({name, unsigned(lockDepth), a, b});\n'
                          '  std::printf("%s %d %d\\n", name, a, b);\n}')
    source = replace_once(source,
                          'menuRuntime.render_lock = []() -> void* { assert(!menuModelDepth); return new RenderLock; };',
                          'menuRuntime.render_lock = []() -> void* {\n'
                          '    assert(!menuModelDepth && !lockDepth);\n'
                          '    auto* token = new RenderLock;\n'
                          '    ++admissionAcquisitions;\n'
                          '    if (admissionAcquisitions == admissionRejectAt) {\n'
                          '      assert(activeMenu && activeMenu->rust && lockDepth == 1);\n'
                          '      xp_epub_test_overlay_reject(activeMenu->rust);\n'
                          '    }\n'
                          '    return token;\n  };')
    # Observe physical child ownership without replacing the child constructor/sink.
    for before, record in (
        ('std::printf("new [%s] %d\\n", name, lockDepth);', 'child-new'),
        ('std::printf("drop [%s] %d\\n", kind, lockDepth);', 'child-drop'),
        ('std::printf("start [%s] %d\\n", value->kind, lockDepth);', 'child-start'),
    ):
        source = replace_once(source, before,
                              'admissionEvents.push_back({"' + record + '", unsigned(lockDepth)}); ' + before)
    return source + COMMON + (HERE / 'more_admission_main.inc').read_text()


def mutations(sources):
    # Main finalizes anchors after production is pinned. No global replacements.
    guard = re.compile(r'if\s+(?:owner|self)\.model\(\|state\|\s*state\.scene_gate\.failed\)\s*\{')
    original = sources['toc']
    begin = original.index('    fn activate_contents_row(')
    end = original.index('    fn activate_overlay_row(', begin)
    scope = original[begin:end]
    sites = list(guard.finditer(scope))
    assert len(sites) == 1, ('expected one unified TOC admission guard', len(sites))
    site = sites[0]
    changed = original[:begin] + scope[:site.start()] + 'if false {' + scope[site.end():] + original[end:]
    yield 'toc-guard-removed', 'toc', changed, [(0, 1), (1, 1)], 'TOC_ADMISSION'
    original = sources['more']
    begin = original.index('    pub(super) fn activate_more_row(')
    scope = original[begin:]
    sites = list(guard.finditer(scope))
    assert len(sites) == 2, ('expected initial and unified second More admission guards', len(sites))
    for ordinal, site in enumerate(sites, 1):
        changed = original[:begin] + scope[:site.start()] + 'if false {' + scope[site.end():]
        marker = 'MORE_FIRST_ADMISSION' if ordinal == 1 else 'MORE_SECOND_ADMISSION'
        yield 'more-guard-%d-removed' % ordinal, 'more', changed, [(0, ordinal), (1, ordinal)], marker


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--archive', type=Path, help='baseline app-test archive, without reader-strips')
    parser.add_argument('--evidence-dir', type=Path, help='new directory; refuse overwrite')
    args = parser.parse_args()
    root = args.repo.resolve()
    oracle = root / 'test/reader_lifecycle'
    sys.dont_write_bytecode = True
    sys.path[:0] = [str(oracle), str(root / 'test')]
    overlay = import_adapter('admission_overlay', oracle / 'test_overlay_input.py')
    menu = import_adapter('admission_menu', oracle / 'test_menu_parent.py')
    from rust_host import rust_link_flags
    fixtures = {'toc': toc_fixture(overlay), 'more': more_fixture(menu, root)}
    sources = {name: (root / 'rust' / path).read_text() for name, path in PRODUCTION.items()}
    mutants = list(mutations(sources))
    goldens = {path: path.read_bytes() for path in oracle.glob('menu-parent-*.golden.gz')}
    assert len(goldens) == 4
    goldens[oracle / overlay.GOLDEN] = (oracle / overlay.GOLDEN).read_bytes()
    assert digest(goldens[oracle / overlay.GOLDEN]) == overlay.GZIP_SHA
    assert digest(gzip.decompress(goldens[oracle / overlay.GOLDEN])) == overlay.RAW_SHA
    for strip, (expected, _) in menu.GOLDENS.items():
        assert digest(goldens[oracle / ('menu-parent-%d.golden.gz' % strip)]) == expected
    paths = list((root / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    paths += [oracle / name for name in ('test_overlay_input.py', 'overlay_oracle.cpp',
              'overlay_input_session_rust.inc', 'overlay_input_session_fixture.h', 'overlay_actions_session_fixture.h',
              'test_menu_parent.py', 'menu_parent_oracle.cpp', 'menu_session_fixture.h', 'menu_session_rust.inc')]
    paths += [root / name for name in ('src/MappedInputManager.h', 'src/util/ButtonNavigator.cpp',
                                      'rust/crates/xp-ffi/include/xp_epub_session.h')]
    hashes = {str(path.relative_to(root)): digest(path.read_bytes()) for path in paths}
    draft_hashes = {path.name: digest(path.read_bytes()) for path in HERE.iterdir() if path.is_file()}
    try:
        with tempfile.TemporaryDirectory(prefix='toc-more-admission-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=False)
            commands, records = [], []
            for name, source in fixtures.items():
                (work / (name + '.cpp')).write_text(source)
                (evidence / (name + '.cpp')).write_text(source)
            def command(argv, **kwargs):
                commands.append([str(arg) for arg in argv])
                return subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
            def log(name, result):
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
            def compile_fixture(kind, name, archive=None):
                flags = rust_link_flags() if archive is None else rust_link_flags(archive=str(archive))
                i18n = menu.generated_i18n_directory(flags[-1])
                binary = work / name
                result = command(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                  '-fno-sanitize-recover=all', '-DXP_STRIP_RENDER=0', '-I', str(oracle),
                                  '-I', str(i18n), str(work / (kind + '.cpp')), *flags, '-o', str(binary)])
                log(name + '.compile', result)
                assert result.returncode == 0, ('fixture compile failure', name, result.stderr.decode(errors='replace'))
                return binary
            def run(binary, case, name):
                result = command([str(binary), *map(str, case)], timeout=60)
                log(name, result)
                return result
            for kind, cases in [('toc', [(valid, rejected) for valid in (0, 1) for rejected in (0, 1)]),
                                ('more', [(percent, rejected) for percent in (0, 1) for rejected in (0, 1, 2)])]:
                binary = compile_fixture(kind, kind + '-baseline', args.archive)
                for case in cases:
                    name = kind + '-baseline-%d-%d' % case
                    result = run(binary, case, name)
                    assert result.returncode == 0 and ('PASS ' + kind).encode() in result.stdout, (
                        'baseline failed', name, result.returncode, result.stderr.decode(errors='replace'))
                    records.append({'name': name, 'returncode': result.returncode, 'stdout_sha256': digest(result.stdout)})
            copied = work / 'source'
            copied_rust = copied / 'rust'
            shutil.copytree(root / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied / name).symlink_to(root / name, target_is_directory=True)
            target = work / 'target'
            for name, kind, changed, cases, marker in mutants:
                # Reset both sources each iteration: each archive contains exactly one mutant.
                for key, path in PRODUCTION.items():
                    (copied_rust / path).write_text(changed if key == kind else sources[key])
                result = command(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                  '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                  '--features', 'app-test'], cwd=copied_rust)
                log(name + '.cargo', result)
                assert result.returncode == 0, ('mutant did not compile', name)
                binary = compile_fixture(kind, name, target / 'release/libxp_ffi.a')
                (evidence / (name + '.rs')).write_text(changed)
                for case in cases:
                    result = run(binary, case, name + '-%d-%d' % case)
                    assert result.returncode == 42 and result.stderr == ('EXPECTED_ASSERTION: ' + marker + '\n').encode(), (
                        'mutant must fail the specific admission assertion, not abort', name, case,
                        result.returncode, result.stderr.decode(errors='replace'))
                    records.append({'mutant': name, 'case': case, 'killed': True, 'assertion': marker,
                                    'original_sha256': digest(sources[kind].encode()), 'mutant_sha256': digest(changed.encode()),
                                    'stdout_sha256': digest(result.stdout), 'returncode': result.returncode})
            manifest = {'test_name': 'toc_more_admission', 'baseline_cases': 10, 'mutants': 3, 'mutant_runs': 6,
                        'limits': LIMITS, 'source_sha256': hashes, 'draft_sha256': draft_hashes,
                        'goldens': {path.name: {'gzip_sha256': digest(data), 'raw_sha256': digest(gzip.decompress(data))}
                                    for path, data in goldens.items()}, 'records': records, 'commands': commands}
            (evidence / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
            print(json.dumps(manifest), flush=True)
    finally:
        for path, data in goldens.items():
            assert path.read_bytes() == data, ('immutable golden changed', path)
        for path, expected in hashes.items():
            assert digest((root / path).read_bytes()) == expected, ('source changed during draft execution', path)


if __name__ == '__main__':
    main()
