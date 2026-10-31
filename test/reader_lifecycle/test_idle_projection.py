"""Compare idle input/effect projection against the original complete C++ loop."""
from pathlib import Path
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
current = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
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
main = r'''
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
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    flags=['c++','-std=c++17','-UNDEBUG','-I'+str(ROOT/'src')]
    link=rust_link_flags()
    def run(body, idle=''):
        code=fixture.replace('#include "loop_methods.inc"', body+'\n'+idle)+main
        cpp=path/'idle.cpp';cpp.write_text(code)
        subprocess.run([*flags,str(cpp),*link,'-o',str(path/'idle')],check=True)
        return project(subprocess.check_output([str(path/'idle')]))
    expected=run(loop)
    golden=HERE/'idle-projection.golden.gz'
    if '--freeze-original' in sys.argv:
        golden.write_bytes(gzip.compress(expected,mtime=0))
    assert expected==gzip.decompress(golden.read_bytes())
    idle=definition(current,'bool EpubReaderActivity::tryIdleLoop(')
    candidate=loop.replace('void EpubReaderActivity::loop() {','void EpubReaderActivity::loop() {\n  if (tryIdleLoop()) return;',1)
    assert run(candidate,idle)==expected
    mutations=[
      idle.replace('  mappedInput.wasBackGesture();',''),
      idle.replace('  mappedInput.wasBackGesture();','  mappedInput.wasBackGesture();\n  mappedInput.wasBackGesture();'),
      idle.replace('  mappedInput.wasBackGesture();','').replace('  mappedInput.wasReleased(MappedInputManager::Button::Back);','  mappedInput.wasReleased(MappedInputManager::Button::Back);\n  mappedInput.wasBackGesture();'),
      idle.replace('  return true;','  requestUpdate();\n  return true;'),
      idle.replace('sample.bookmark_notice = showBookmarkMessage;','sample.bookmark_notice = 0;'),
      idle.replace('sample.automatic = automaticPageTurnActive;','sample.automatic = 0;'),
      idle.replace('sample.held_buttons |= 1u << button;','sample.held_buttons |= 0;'),
    ]
    for index, mutation in enumerate(mutations):
        assert mutation!=idle and run(candidate,mutation)!=expected, index
print('idle projection: original full C++ loop, 36 cases, consuming probe order/effects/model changes, and 7 mutations pass')
