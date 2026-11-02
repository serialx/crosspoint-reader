"""Check font-picker admission at both ResourceAccess boundaries.

This reuses the real overlay-input owner and existing fixture hooks. It does not
capture, project, or overwrite a golden; rejection is injected deterministically.
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


PRODUCTION = Path('crates/xp-ffi/src/app/epub/overlay_input.rs')
LIMITS = ('Deterministic rejection injected after physical ResourceAccess acquisition; '
          'not a real-thread race, full A1, or device test. Existing settle/discard/text '
          'and navigation fixture hooks remain compositional boundaries.')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, old, new):
    assert source.count(old) == 1, ('copy transform anchor', old, source.count(old))
    return source.replace(old, new, 1)


PREAMBLE = r'''
#include <cstring>
#include <stdexcept>
extern "C" void xp_epub_test_overlay_reject(void*);
struct AdmissionEvent { std::string name; unsigned depth; };
static std::vector<AdmissionEvent> admissionEvents;
static unsigned admissionAcquisitions = 0, admissionRejectAt = 0;
static unsigned countEvent(const char* name) {
  return std::count_if(admissionEvents.begin(), admissionEvents.end(),
                       [name](const AdmissionEvent& e) { return e.name == name; });
}
static void requireAdmission(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
'''


MAIN = r'''
static bool sameObservation(const XpEpubTestOverlayObservation& a,
                            const XpEpubTestOverlayObservation& b) {
  return a.focus == b.focus && a.index == b.index && a.counter == b.counter &&
         a.overlay == b.overlay && a.cursor == b.cursor && a.hold == b.hold &&
         a.stored == b.stored && a.toolbar == b.toolbar && a.failed == b.failed;
}
static void admissionCase(unsigned rejectAt) {
  admissionRejectAt = rejectAt;
  admissionAcquisitions = 0;
  eventIndex = 0;
  admissionEvents.clear();
  EpubReaderActivity owner;
  owner.overlay = EpubReaderActivity::Overlay::Text;
  owner.panelIndex = 0;
  owner.panelCursorShown = true;
  owner.overlayPageStored = true;
  // Confirm takes the ordinary row-0 path; no Back exception or active popup input.
  owner.mappedInput.released = 1u << int(MappedInputManager::Button::Confirm);
  OverlayInputSessionBridge session(owner);
  XpEpubTestOverlayObservation before{};
  xp_epub_test_overlay_observe(session.rust, &before);
  requireAdmission(before.overlay == 3 && before.stored && !before.failed,
                   "seed is not a clean Text row zero");
  owner.handleOverlayInput();
  XpEpubTestOverlayObservation after{};
  xp_epub_test_overlay_observe(session.rust, &after);
  const unsigned dismiss = countEvent("dismiss"), discard = countEvent("discard");
  const unsigned settle = countEvent("settle"), child = countEvent("start-child");
  const unsigned update = countEvent("update");
  std::printf("admission reject=%u acquisitions=%u dismiss=%u discard=%u settle=%u child=%u update=%u mode=%u failed=%u\n",
              rejectAt, admissionAcquisitions, dismiss, discard, settle, child, update,
              unsigned(after.overlay), unsigned(after.failed));
  if (rejectAt == 1) {
    requireAdmission(admissionAcquisitions == 1 && dismiss == 0 && discard == 0 &&
                     settle == 0 && child == 0 && update == 1 && after.overlay == 3 &&
                     after.stored && after.failed,
                     "FONT_PICKER_FIRST_ADMISSION");
  } else if (rejectAt == 2) {
    requireAdmission(admissionAcquisitions == 2 && dismiss == 1 && discard == 1 &&
                     settle == 1 && child == 0 && update == 1 && after.overlay == 3 &&
                     !after.stored && after.failed,
                     "FONT_PICKER_SECOND_ADMISSION");
  } else {
    requireAdmission(admissionAcquisitions == 2 && dismiss == 1 && discard == 1 &&
                     settle == 1 && child == 1 && update == 0 && after.overlay == 0 &&
                     !after.stored && !after.failed,
                     "FONT_PICKER_SUCCESS");
  }
  requireAdmission(countEvent("font-picker-create") == child &&
                   countEvent("font-picker-drop") == child && !lockDepth && !overlayInputModelDepth,
                   "child ownership or lock imbalance");
  requireAdmission(after.focus == before.focus && after.index == before.index &&
                   after.counter == before.counter && after.cursor == before.cursor &&
                   after.hold == before.hold && after.toolbar == before.toolbar,
                   "admission changed unrelated overlay fields");
  std::vector<std::string> transitions;
  for (const auto& e : admissionEvents) {
    if (e.name == "dismiss" || e.name == "discard" ||
        e.name == "settle" || e.name == "registry" || e.name == "font-picker-create" ||
        e.name == "start-child" || e.name == "font-picker-drop" || e.name == "update")
      transitions.push_back(e.name);
  }
  const std::vector<std::string> expected = rejectAt == 1
      ? std::vector<std::string>{"update"}
      : rejectAt == 2
      ? std::vector<std::string>{"dismiss", "discard", "settle", "update"}
      : std::vector<std::string>{"dismiss", "discard", "settle",
                                 "registry", "font-picker-create", "start-child", "font-picker-drop"};
  requireAdmission(transitions == expected, "admission transition event order");
  for (const auto& e : admissionEvents) {
    if (e.name == "dismiss" || e.name == "discard" || e.name == "settle")
      requireAdmission(e.depth == 1, "transition outside ResourceAccess");
    if (e.name == "font-picker-create" || e.name == "start-child" || e.name == "font-picker-drop")
      requireAdmission(e.depth == 0, "child launch inside ResourceAccess");
  }
  if (rejectAt) {
    requireAdmission(!owner.result && countEvent("text-settings") == 0,
                     "rejected picker installed a child callback");
    const auto eventCount = admissionEvents.size();
    XpEpubTestOverlayInputObservation inputBefore{}, inputAfter{};
    xp_epub_test_overlay_input_observe(session.rust, &inputBefore);
    const auto anchor = owner.pendingAnchor;
    // An accidental TextSettings intent executes apply_text_settings even for null.
    xp_epub_test_overlay_input_result(session.rust, nullptr);
    session.observe();
    XpEpubTestOverlayObservation resultState{};
    xp_epub_test_overlay_observe(session.rust, &resultState);
    xp_epub_test_overlay_input_observe(session.rust, &inputAfter);
    requireAdmission(admissionEvents.size() == eventCount && sameObservation(after, resultState) &&
                     inputBefore.spine == inputAfter.spine && inputBefore.next_page == inputAfter.next_page &&
                     inputBefore.section == inputAfter.section && anchor == owner.pendingAnchor,
                     "rejected picker retained intent or changed result state");
  } else {
    // Positive control: the same null-result probe observes a real installed intent.
    xp_epub_test_overlay_input_result(session.rust, nullptr);
    session.observe();
    requireAdmission(countEvent("text-settings") == 1 && countEvent("update") == 1 &&
                     owner.overlay == EpubReaderActivity::Overlay::Text,
                     "null-result positive control did not consume picker intent");
  }
  std::printf("PASS admission %u\n", rejectAt);
}
int main(int argc, char** argv) {
  if (argc != 2 || std::strlen(argv[1]) != 1 || argv[1][0] < '0' || argv[1][0] > '2') return 64;
  try { admissionCase(unsigned(argv[1][0] - '0')); }
  catch (const std::exception& error) {
    std::fprintf(stderr, "EXPECTED_ASSERTION: %s\n", error.what());
    return 42;
  }
  return 0;
}
'''


def fixture_source(adapter):
    source = adapter.transformed_fixture()
    source, old_main = source.split('int main() {', 1)
    assert old_main
    source = replace_once(source, 'static unsigned eventIndex = 0, lockDepth = 0;',
                          PREAMBLE + '\nstatic unsigned eventIndex = 0, lockDepth = 0;')
    source = replace_once(source, '  std::printf("%u %s %d %d lock=%u\\n", eventIndex++, name, a, b, lockDepth);',
                          '  admissionEvents.push_back({name, lockDepth});\n'
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
    old = ('overlayInputLeaves.popup_dismiss = [](void* raw) {\n'
           '    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == &owner.overlayPopup);')
    new = old.replace('!lockDepth', 'lockDepth == 1')
    # Main may have already narrowed the repository fixture after the pin.
    if old in source:
        source = replace_once(source, old, new)
    else:
        assert source.count(new) == 1, 'unexpected popup dismissal adapter shape'
    return source + MAIN


def guard_mutants(original):
    begin = original.index('    fn activate_overlay_row(')
    first = original.index('                0 => {', begin)
    end = original.index('                4 => {', first)
    scope = original[first:end]
    # Require the two production checks inside this one transition.
    pattern = r'if\s+self\.model\(\|state\|\s*state\.scene_gate\.failed\)\s*\{'
    sites = list(re.finditer(pattern, scope))
    assert len(sites) == 2, ('expected two Text-row-0 admission guards', len(sites))
    for ordinal, site in enumerate(sites, 1):
        changed = scope[:site.start()] + 'if false {' + scope[site.end():]
        yield ordinal, original[:first] + changed + original[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path.cwd())
    parser.add_argument('--evidence-dir', type=Path, help='new directory; refuses to overwrite')
    args = parser.parse_args()
    root = args.repo.resolve()
    here = root / 'test/reader_lifecycle'
    sys.dont_write_bytecode = True
    sys.path[:0] = [str(here), str(root / 'test')]
    spec = importlib.util.spec_from_file_location('font_admission_overlay_adapter', here / 'test_overlay_input.py')
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    from rust_host import rust_link_flags
    golden_path = here / adapter.GOLDEN
    golden = golden_path.read_bytes()
    assert digest(golden) == adapter.GZIP_SHA
    assert digest(gzip.decompress(golden)) == adapter.RAW_SHA
    paths = list((root / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs'))
    paths += [here / name for name in ('test_overlay_input.py', 'overlay_oracle.cpp',
              'overlay_input_session_rust.inc', 'overlay_input_session_fixture.h',
              'overlay_actions_session_fixture.h')]
    paths += [root / name for name in ('src/MappedInputManager.h', 'src/util/ButtonNavigator.cpp',
                                      'rust/crates/xp-ffi/include/xp_epub_session.h')]
    hashes = {str(path.relative_to(root)): digest(path.read_bytes()) for path in paths}
    production = (root / 'rust' / PRODUCTION).read_text()
    mutants = list(guard_mutants(production))
    source = fixture_source(adapter)
    try:
        with tempfile.TemporaryDirectory(prefix='font-picker-admission-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=False)
            cpp = work / 'font_picker.cpp'
            cpp.write_text(source)
            (evidence / 'fixture.cpp').write_text(source)
            commands = []
            def command(argv, **kwargs):
                commands.append([str(arg) for arg in argv])
                (evidence / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
                return subprocess.run(argv, **kwargs)
            def compile_fixture(name, archive=None):
                binary = work / name
                flags = rust_link_flags() if archive is None else rust_link_flags(archive=str(archive))
                result = command(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                  '-fno-sanitize-recover=all',
                                  '-I', str(here), str(cpp), *flags, '-o', str(binary)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.compile.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.compile.stderr')).write_bytes(result.stderr)
                assert result.returncode == 0, ('fixture compile failed', name, result.stderr.decode(errors='replace'))
                return binary
            def run(binary, case, name):
                result = command([str(binary), str(case)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                return result
            records = []
            baseline = compile_fixture('baseline')
            for case in range(3):
                result = run(baseline, case, 'baseline-' + str(case))
                assert result.returncode == 0, (case, result.stderr.decode(errors='replace'))
                assert ('PASS admission %d\n' % case).encode() in result.stdout
                records.append({'case': case, 'stdout_sha256': digest(result.stdout), 'returncode': result.returncode})
            copied = work / 'source'
            copied_rust = copied / 'rust'
            shutil.copytree(root / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied / name).symlink_to(root / name, target_is_directory=True)
            target = work / 'target'
            for ordinal, changed in mutants:
                name = 'guard-%d-removed' % ordinal
                (copied_rust / PRODUCTION).write_text(changed)
                result = command(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                  '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                  '--features', 'app-test'], cwd=copied_rust,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.cargo.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.cargo.stderr')).write_bytes(result.stderr)
                assert result.returncode == 0, ('mutant did not build', name)
                binary = compile_fixture(name, target / 'release/libxp_ffi.a')
                result = run(binary, ordinal, name)
                marker = 'FONT_PICKER_FIRST_ADMISSION' if ordinal == 1 else 'FONT_PICKER_SECOND_ADMISSION'
                assert result.returncode == 42 and result.stderr == ('EXPECTED_ASSERTION: ' + marker + '\n').encode(), (
                    'mutant must fail the specific admission assertion, not abort', name,
                    result.returncode, result.stderr.decode(errors='replace'))
                records.append({'mutant': name, 'case': ordinal, 'killed': True, 'assertion': marker,
                                'production_sha256': digest(production.encode()),
                                'mutant_sha256': digest(changed.encode()), 'stdout_sha256': digest(result.stdout)})
            summary = {'test_name': 'reader_font_picker_admission', 'cases': 3, 'mutants': 2,
                       'limits': LIMITS, 'golden_gzip_sha256': adapter.GZIP_SHA,
                       'golden_raw_sha256': adapter.RAW_SHA, 'source_sha256': hashes,
                       'fixture_sha256': digest(source.encode()), 'records': records, 'commands': commands}
            (evidence / 'manifest.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
    finally:
        assert golden_path.read_bytes() == golden, 'immutable input golden changed'
        for path, expected in hashes.items():
            assert digest((root / path).read_bytes()) == expected, ('input source changed', path)


if __name__ == '__main__':
    main()
