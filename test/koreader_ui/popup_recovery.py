"""Exercise production KOReader popup handling after a rejected settings scene."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
cpp = (ROOT/'src/activities/settings/KOReaderSettingsActivity.cpp').read_text()
header = (ROOT/'src/activities/settings/KOReaderSettingsActivity.h').read_text()
code = r'''
#include <cassert>
#include <utility>
int critical = 0;
struct RenderLock {};
struct Popup {
  bool active = false;
  unsigned inputs = 0, paints = 0, dismissals = 0;
  bool isActive() const { return active; }
  void dismiss() { assert(!critical); active = false; ++dismissals; }
  template<class F> bool handleInput(int, F) { if(active) ++inputs; return active; }
  bool processRender(int,int) { if(active) ++paints; return active; }
};
struct SettingsUiActivity {
  bool failed = false;
  unsigned paints = 0;
  struct ModelLock { ModelLock(SettingsUiActivity&) {++critical;} ~ModelLock(){--critical;} };
  bool uiSceneFailed() const {return failed;}
  void rustSceneFailed() {failed = true;}
  void render(RenderLock&&) {++paints;failed = false;}
};
struct KOReaderSettingsActivity : SettingsUiActivity {
  Popup optionPopup;
  bool discardPopup = false;
  int renderer=0,mappedInput=0;
  unsigned requests=0;
  void requestUpdate(){++requests;}
  bool handleCustomInput();
  void render(RenderLock&&);
'''
code += method(header, '  void rustSceneFailed()').replace(' override', '')
code += '\n};\n'
code += method(cpp, 'bool KOReaderSettingsActivity::handleCustomInput()')
code += method(cpp, 'void KOReaderSettingsActivity::render(')
code += r'''
int main() {
  for (bool previouslyPainted : {false,true}) for (bool recoverBeforeInput : {false,true}) {
    KOReaderSettingsActivity activity;
    if(previouslyPainted) activity.render(RenderLock{});
    activity.optionPopup.active = true;
    // Ordinary input before first paint retains the existing popup behavior.
    assert(activity.handleCustomInput() && activity.optionPopup.inputs == 1);
    activity.rustSceneFailed();
    assert(activity.failed && activity.discardPopup);
    if(recoverBeforeInput) {
      activity.render(RenderLock{});
      assert(!activity.failed && !activity.optionPopup.paints);
    }
    assert(activity.handleCustomInput());
    assert(!activity.optionPopup.active && activity.optionPopup.inputs == 1);
    assert(activity.optionPopup.dismissals == 1 && !activity.discardPopup);
    if(!recoverBeforeInput) assert(!activity.handleCustomInput());
    activity.render(RenderLock{});
    activity.optionPopup.active = true;
    assert(activity.handleCustomInput() && activity.optionPopup.inputs == 2);
  }
}
'''
with tempfile.TemporaryDirectory(prefix='ko-popup-recovery-') as directory:
    p = Path(directory)
    (p/'main.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17',str(p/'main.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
print('KOReader popup failure, deferred discard and recovery passed')
