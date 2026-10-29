"""Compile production adapter/effect/delivery/suppression code with host-only leaves."""
from pathlib import Path
import sys
import shutil
repo = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
def method(path, signature):
    text = (repo/path).read_text(); start = text.index(signature); brace = text.index('{', start); level = 1; end = brace+1
    while level:
        level += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]
for name in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:
    shutil.copyfile(repo/'src/activities'/name,out/name)
shutil.copytree(repo/'test/ui_runtime/stubs',out,dirs_exist_ok=True)
shutil.copyfile(repo/'src/activities/ActivityResult.h',out/'ActivityResult.h')
h=out/'components/UiAppHelpers.h'; h.write_text(h.read_text().replace('touchAction(bool)', 'touchAction(bool = false)'))
s=(out/'Activity.h').read_text().replace('#include <xp_ui.h>','#include <xp_ui.h>\n#include "ActivityResult.h"')
s=s.replace('class MappedInputManager {','struct HalGPIO {bool wasReleased() const {return false;}};\nclass MappedInputManager {')
s=s.replace('uint8_t touchFlags = 0;', '''mutable uint16_t suppressedReleaseButtons=0;
  void suppressNextRelease(Button) const;
  bool consumeSuppressedRelease() const;
  bool mapButton(Button b,bool (HalGPIO::*)() const) const {return wasReleased(b);}
  uint8_t touchFlags = 0;''')
s=s.replace('void requestUpdate() {}','void requestUpdate(bool = false) {}').replace('virtual void loop() {}','virtual void onEnter() {}\n  virtual void loop() {}')
s=s.replace('virtual ~Activity() = default;', '''virtual ~Activity() = default;
  ActivityResult result;
  ActivityResultHandler resultHandler;
  bool finished=false;
  void finish(){finished=true;}
  void setResult(ActivityResult&& r){result=std::move(r);}''')
(out/'Activity.h').write_text(s)
s=(repo/'test/ui_utility/parents.cpp').read_text()
effects='\n'.join(method(p,sig) for p,sig in [
 ('src/activities/util/UtilityActivity.cpp','void UtilityActivity::onEnter('),
 ('src/activities/util/UtilityActivity.cpp','void UtilityActivity::applyUtilityEffect('),
 ('src/activities/util/IntervalSelectionActivity.cpp','void IntervalSelectionActivity::applyUtilityEffect('),
 ('src/MappedInputManager.cpp','void MappedInputManager::suppressNextRelease('),
 ('src/MappedInputManager.cpp','bool MappedInputManager::consumeSuppressedRelease(')])
s=s.replace('// PRODUCTION_EFFECTS',effects)
manager=(repo/'src/activities/ActivityManager.cpp').read_text()
a=manager.index('auto handler = std::move(currentActivity->resultHandler);');b=manager.index('handler(pendingResult);',a)+len('handler(pendingResult);')
s=s.replace('// PRODUCTION_DELIVERY',manager[a:b])
(out/'parents.cpp').write_text(s)
