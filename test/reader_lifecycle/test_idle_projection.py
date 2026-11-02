"""Compare ordinary Rust idle admission with the frozen complete C++ loop.

The original consuming/effect projection stays unchanged. Raw traces are also
saved; the complete historical loop remains the candidate's fallback.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import json
import os
import shutil
import gzip
import re
import subprocess
import sys
import tempfile
from extract import definition
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
original = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/EpubReaderActivity.cpp'], cwd=ROOT, text=True)
utils = subprocess.check_output(['git', 'show', '8d534a99:src/activities/reader/ReaderUtils.h'], cwd=ROOT, text=True)
fixture = (HERE / 'loop_oracle.cpp').read_text()
fixture = fixture[:fixture.index('int main()')]
fixture = fixture.replace('  bool tryIdleLoop() { return false; }', '  bool tryIdleLoop();')
fixture = fixture.replace('enum SHORT_PWRBTN { FOOTNOTES', 'enum SHORT_PWRBTN { PAGE_TURN = 2, FOOTNOTES')
fixture = fixture.replace('  uint8_t orientation =', '''  enum { OFF = 0, TAP_AND_SWIPE = 0, TAP_ONLY = 1, SWIPE_ONLY = 2, INVERTED_TAP = 3,
    READER_MENU_OFF = 0, READER_MENU_TAP = 1, READER_MENU_SWIPE_UP = 2 };
  bool tiltPageTurn = false, backShortToFileBrowser = false;
  uint8_t pageTurnGesture = 0, previousPageGesture = 0, showReaderMenu = 0;
  uint8_t orientation =''')
fixture = fixture.replace('enum class Button { Back, Confirm, Power, Down };', 'enum class Button { Back, Confirm, Power, Down, Left, Right, PageBack, PageForward };\n  enum class SwipeDir { None, Left, Right, Up, Down };')
fixture = fixture.replace('unsigned released = 0, pressed = 0, suppressed = 0, fired = 0;', 'unsigned released = 0, pressed = 0; mutable unsigned suppressed = 0, fired = 0;\n  bool remote = false;')
fixture = fixture.replace('unsigned long threshold) {', 'unsigned long threshold) const {')
fixture = fixture.replace('  bool wasReleased(Button b) const {', '  bool wasReleased(Button b) const {\n    if (b == Button::Back) wasBackGesture();')
fixture = fixture.replace('  bool hasTouch() const {', '''  bool hasRemoteClick() const { return remote; }
  bool wasPressed(Button b) const { event("press", int(b)); return pressed & (1u << int(b)); }
  bool isNavDirectionSwapped() const { return false; }
  SwipeDir wasSwipe() const { event("consume-swipe"); return SwipeDir::None; }
  bool wasBackGesture() const { event("consume-back"); return false; }
  bool wasMenuGesture() const { event("consume-menu"); return false; }
  bool wasReaderMenuSwipeUp() const { event("consume-menu-up"); return false; }
  bool hasTouch() const {''')
fixture = fixture.replace('struct GfxRenderer {', 'struct GfxRenderer {\n  int getScreenWidth() const { return 90; }\n  int getScreenHeight() const { return 120; }')
start = fixture.index('namespace ReaderUtils {')
end = fixture.index('}  // namespace ReaderUtils', start) + len('}  // namespace ReaderUtils')
names = ['inline bool gestureAllowsSwipe(', 'inline bool gestureAllowsTap(', 'inline bool isRtlBookLanguage(',
         'inline PageTurnResult detectPageTurn(', 'inline TouchPageTurn detectTouchPageTurn(',
         'inline bool isTouchMenuTap(', 'inline bool isTouchMenuGesture(', 'inline bool handleBackNavigation(']
helpers = '\n'.join(definition(utils, name) for name in names)
fixture = fixture[:start]+'''
struct ActivityManager { void goToFileBrowser(const char*) { event("browser"); } } manager;
struct HalGPIO {
  enum { BTN_BACK = 0, BTN_POWER = 6 };
  uint8_t pressed = 0, edges = 0;
  bool wasAnyPressed() const { return edges; }
  bool wasAnyReleased() const { return edges; }
  bool isPressed(uint8_t button) const { return pressed & (1u << button); }
  unsigned long lastTouchHeldMs() const { return 0; }
} gpio;
struct { bool wasTiltedForward() { event("tilt-next"); return false; } bool wasTiltedBack() { event("tilt-prev"); return false; } } halTiltSensor;
namespace ReaderUtils {
constexpr unsigned long SKIP_HOLD_MS = 700, GO_HOME_MS = 1000, GO_BACK_OR_HOME_MS = 1000, BOOKMARK_MESSAGE_DURATION_MS = 2500;
struct PageTurnResult { bool prev, next, fromTilt; };
struct TouchPageTurn { bool prev, next; unsigned long heldMs; };
struct BackNavCallback { void* ctx; void (*fn)(void*); };
'''+helpers+'\n}\n'+fixture[end:]
fixture = fixture.replace('  Popup loadFailurePopup;', '  XpUiReaderBaseModel baseModel{};\n  std::unique_ptr<int> endOfBookOptions;\n  Popup loadFailurePopup;')
fixture = fixture.replace('    event("back", backHandled);\n    return backHandled;', '    return ReaderUtils::handleBackNavigation(mappedInput, manager, "book", {nullptr, [](void*) { event("home-effect"); }});')
# The frozen loop contains no idle admission. The candidate changes only its entry.
loop = definition(original, 'void EpubReaderActivity::loop()')
fixture_main = r'''
int main() {
  for (int touchControls : {0, 1}) for (int test = 0; test < 18; ++test) {
    EpubReaderActivity r;
    SETTINGS = {}; gpio = {}; busy = true; lockAvailable = false;
    clockMs = 3000; logIndex = 0;
    SETTINGS.touchReaderControls = touchControls;
    r.baseModel.book_remembered = 1;
    r.confirmThreshold = 0;
    switch(test) {
      case 1: r.showBookmarkMessage = true; break;
      case 2: r.showDictionaryMessage = true; break;
      case 3: r.showFontDamagedMessage = true; break;
      case 4: r.showClippingMessage = true; break;
      case 5: r.automaticPageTurnActive = true; break;
      case 6: r.pendingManualTurn = 1; break;
      case 7: gpio.pressed = 1; r.mappedInput.pressed = 1; r.mappedInput.held = 1000; break;
      case 8: gpio.edges = 1; r.mappedInput.released = 2; break;
      case 9: r.mappedInput.remote = true; break;
      case 10: r.mappedInput.home = HomeButtonAction::ReaderMenu; break;
      case 11: r.baseModel.book_remembered = 0; break;
      case 12: r.overlay = EpubReaderActivity::Overlay::More; break;
      case 13: r.appliedOrientation = 1; break;
      case 14: r.confirmThreshold = 400; break;
      case 15: SETTINGS.longPressButtonBehavior = 1; break;
      case 16: SETTINGS.tiltPageTurn = true; break;
      case 17: r.mappedInput.touch = true; break;
    }
    std::printf("case %d %d\n", touchControls, test);
    r.loop();
    state(r);
  }
}
'''
# Every potentially consuming mapped method and every externally visible effect is retained.
consuming = {'touch-long', 'long', 'tap', 'consume-back', 'consume-swipe', 'consume-menu', 'consume-menu-up', 'tilt-next', 'tilt-prev'}
effects = {'update', 'finish', 'orientation', 'radio-after', 'prewarm-start', 'prewarm-end', 'page-render', 'build-start',
           'recent-add', 'recent-remove', 'overlay-input', 'overlay-discard', 'clip', 'turn', 'note-turn', 'haptic',
           'bookmark', 'sync', 'dictionary', 'restore', 'footnote', 'overlay-open', 'reader-menu', 'href', 'browser', 'home-effect', 'skip'}
def project(raw):
    result=[]
    for line in raw.decode().splitlines():
        bits=line.split()
        if bits[0] in {'case','state'}: result.append(line)
        elif bits[1] in consuming|effects or (bits[1]=='release' and bits[2]=='0'):
            result.append(' '.join(bits[1:]))
    return ('\n'.join(result)+'\n').encode()

GZIP_SHA = '8cc232537c42d4682106a760bf428f0baf5bac3fec8922c5d80dd0c621a8daf1'
RAW_SHA = '7c9a3125ccb5d92c84f9542648de135cccc7e3bd3e2068e8d3c96bdab853d1da'
PRODUCTION = Path('crates/xp-ffi/src/app/epub/idle.rs')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(source, before, after):
    assert source.count(before) == 1, ('expected one replacement', before)
    return source.replace(before, after, 1)


def mutants(source):
    back = '        self.mapped_query(Query::BackGesture);\n'
    release = '        self.mapped_button(BACK, ButtonQuery::Release, 0);\n'
    yield 'back-gesture-omitted', replace_once(source, back, '')
    yield 'back-gesture-duplicated', replace_once(source, back, back + back)
    yield 'back-gesture-reordered', replace_once(replace_once(source, back, ''), release, release + back)
    # No App/navigation hooks are installed solely to make an update mutant observable.
    yield 'extra-screen-long-probe', replace_once(source, '        true\n', '''        let (mut x, mut y) = (0, 0);
        unsafe { (self.leaves.input_screen_long)(&mut x, &mut y); }
        true
''')
    for name, before in (
        ('bookmark-notice-suppressed', 'bookmark_notice: u8::from(state.bookmark_notice.is_some()),'),
        ('automatic-suppressed', 'automatic: u8::from(state.auto_turn),'),
        ('held-buttons-suppressed', 'held_buttons: input.raw_held as u8,'),
    ):
        yield name, replace_once(source, before, before.split(':', 1)[0] + ': 0,')


def mismatch(actual, expected):
    left, right = actual.splitlines(), expected.splitlines()
    for index, (a, b) in enumerate(zip(left, right)):
        if a != b:
            return {'line': index + 1,
                    'actual': [line.decode(errors='replace') for line in left[max(0, index - 4):index + 5]],
                    'expected': [line.decode(errors='replace') for line in right[max(0, index - 4):index + 5]]}
    return {'actual_lines': len(left), 'expected_lines': len(right)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--freeze-original', action='store_true')
    parser.add_argument('--evidence-dir', type=Path, default=os.environ.get('XP_IDLE_EVIDENCE_DIR'))
    args = parser.parse_args()
    golden_path = HERE / 'idle-projection.golden.gz'
    frozen = golden_path.read_bytes()
    paths = [HERE / name for name in ('loop_oracle.cpp', 'idle_session_fixture.h', 'idle_session_rust.inc',
                                     'overlay_actions_session_fixture.h', 'test_idle_projection.py')]
    paths.extend(sorted((ROOT / 'rust/crates/xp-ffi/src/app/epub').rglob('*.rs')))
    paths.extend(ROOT / name for name in ('rust/crates/xp-ui/src/reader/idle.rs',
                                         'rust/crates/xp-ui/src/reader/input.rs',
                                         'rust/crates/xp-ffi/include/xp_epub_session.h',
                                         'rust/crates/xp-ffi/include/xp_ui_reader.h'))
    hashes = {} if args.freeze_original else {str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in paths}
    try:
        with tempfile.TemporaryDirectory(prefix='reader-idle-session-') as directory:
            work = Path(directory)
            evidence = args.evidence_dir / 'reader_idle_projection' if args.evidence_dir else work / 'evidence'
            evidence.mkdir(parents=True, exist_ok=True)
            flags = rust_link_flags()

            def run(name, body, adapter='', archive=None):
                code = replace_once(fixture, '#include "loop_methods.inc"', body + '\n' + adapter) + fixture_main
                cpp = work / (name + '.cpp')
                cpp.write_text(code)
                binary = work / name
                link = flags if archive is None else rust_link_flags(archive=str(archive))
                subprocess.run(['c++', '-std=c++17', '-UNDEBUG', '-O0', '-fsanitize=address,undefined',
                                '-I', str(ROOT / 'src'), '-I', str(HERE), str(cpp), *link, '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                (evidence / (name + '.stdout')).write_bytes(result.stdout)
                (evidence / (name + '.stderr')).write_bytes(result.stderr)
                projected = project(result.stdout) if result.returncode == 0 else None
                if projected is not None:
                    (evidence / (name + '.projected')).write_bytes(projected)
                return result, projected

            baseline, expected = run('frozen-loop', loop)
            assert baseline.returncode == 0, baseline.stderr.decode(errors='replace')
            if args.freeze_original:
                golden_path.write_bytes(gzip.compress(expected, mtime=0))
                return
            assert digest(frozen) == GZIP_SHA and digest(gzip.decompress(frozen)) == RAW_SHA
            assert expected == gzip.decompress(frozen), mismatch(expected, gzip.decompress(frozen))
            assert len(expected.splitlines()) == 231
            assert sum(line.startswith(b'case ') for line in expected.splitlines()) == 36
            production = (ROOT / 'rust' / PRODUCTION).read_text()
            adapter = (HERE / 'idle_session_rust.inc').read_text()
            candidate_loop = replace_once(loop, 'void EpubReaderActivity::loop() {',
                                          'void EpubReaderActivity::loop() {\n  if (tryIdleLoop()) return;')
            candidate, actual = run('candidate', candidate_loop, adapter)
            assert candidate.returncode == 0, candidate.stderr.decode(errors='replace')
            assert actual == expected, mismatch(actual, expected)
            summary = {'test_name': 'reader_idle_projection', 'cases': 36,
                       'original_gzip_sha256': GZIP_SHA, 'original_projection_sha256': RAW_SHA,
                       'projection_lines': len(expected.splitlines()), 'candidate_projection_sha256': digest(actual),
                       'original_raw_sha256': digest(baseline.stdout), 'candidate_raw_sha256': digest(candidate.stdout),
                       'projection_event_counts': dict(collections.Counter(line.split()[0].decode()
                                                                           for line in expected.splitlines())),
                       'source_sha256': hashes, 'frozen_cpp_loop_sha256': digest(loop.encode()),
                       'frozen_cpp_helpers_sha256': digest(helpers.encode()),
                       'adapter_edge': 'fixture threshold 0/400 maps to long_menu Disabled/Bookmark; board_touch=0',
                       'comparison': 'unchanged consuming/effect projection; full original C++ loop is fallback'}
            (evidence / 'baseline.json').write_text(json.dumps(summary, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
            copied_repo = work / 'source'
            copied_rust = copied_repo / 'rust'
            shutil.copytree(ROOT / 'rust', copied_rust, ignore=shutil.ignore_patterns('target', '__pycache__'))
            for name in ('scripts', 'lib', 'src'):
                (copied_repo / name).symlink_to(ROOT / name, target_is_directory=True)
            target = work / 'target'
            for name, changed in mutants(production):
                (copied_rust / PRODUCTION).write_text(changed)
                # A compile failure is never counted as a killed mutant.
                subprocess.run(['cargo', 'build', '--manifest-path', str(copied_rust / 'Cargo.toml'),
                                '--target-dir', str(target), '-p', 'xp-ffi', '--release', '--locked',
                                '--features', 'app-test'], cwd=copied_rust, check=True)
                result, projection = run(name, candidate_loop, adapter, target / 'release/libxp_ffi.a')
                killed = result.returncode != 0 or projection != expected
                record = {'test_name': 'reader_idle_projection', 'mutant': name, 'kind': 'production-rust',
                          'source': str(PRODUCTION), 'original_source_sha256': digest(production.encode()),
                          'mutant_source_sha256': digest(changed.encode()), 'returncode': result.returncode,
                          'raw_trace_sha256': digest(result.stdout), 'stderr_sha256': digest(result.stderr),
                          'projection_sha256': digest(projection) if projection is not None else None,
                          'killed': killed}
                (evidence / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
                print(json.dumps(record), flush=True)
                assert killed, ('actual Rust mutant survived', name)
    finally:
        if not args.freeze_original:
            assert golden_path.read_bytes() == frozen, 'immutable golden changed'
            for path, before in hashes.items():
                assert digest((ROOT / path).read_bytes()) == before, ('source changed during test', path)


if __name__ == '__main__':
    main()
