"""Frozen native contracts: source data only, never compiler/include inputs."""
from pathlib import Path
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ORACLES = HERE / 'native_oracle'
manifest = json.loads((ORACLES / 'manifest.json').read_text())
texts = {}
for row in manifest['inputs']:
    data = (ORACLES / row['file']).read_bytes()
    assert len(data) == row['bytes']
    assert hashlib.sha256(data).hexdigest() == row['sha256']
    assert row['consumers'] and all((HERE / x).is_file() for x in row['consumers'])
    texts[row['file'].removesuffix('.txt')] = data.decode()
assert set(p.name for p in ORACLES.glob('*.txt')) == {r['file'] for r in manifest['inputs']}
base = texts['UtilityActivity.cpp']
assert 'commandCapacity * sizeof(XpUiCommand) + 2 * hitCapacity * sizeof(XpUiInteraction)' in base
assert base.index('arena = makeUniqueNoThrow') < base.index('xp_ui_utility_init') < base.index('requestUpdate(')
assert base.index('arena.reset()') < base.index('  Activity::onExit();')
assert 'buildHits ^= 1' in base
assert 'result.isCancelled = true;' in base and 'setResult(ActivityResult{});' in base
assert 'const uint16_t commands, const uint16_t hits' in base
assert 'Publication publication{};' in texts['UtilityActivity.h']
assert 'std::unique_ptr<uint8_t[]> arena;' in texts['UtilityActivity.h']
for name, commands, hits in [('Confirmation', 10, 3), ('IntervalSelection', 18, 5)]:
    text = texts[name + 'Activity.cpp']
    assert re.search(r'XP_UI_UTILITY_\w+,\s*' + str(commands) + r',\s*' + str(hits) + r'\)', text)
    assert 'public UtilityActivity' in texts[name + 'Activity.h']
assert 'std::string heading, body, cancelLabel, confirmLabel;' in texts['ConfirmationActivity.h']
assert 'cancelLabel.empty() ? tr(STR_CANCEL)' in texts['ConfirmationActivity.cpp']
assert 'confirmLabel.empty() ? tr(STR_CONFIRM)' in texts['ConfirmationActivity.cpp']
assert 'char readout[64]{}, hints[2][64]{}, minText[32]{}, maxText[32]{};' in texts['IntervalSelectionActivity.h']
assert 'IntervalResult{static_cast<uint32_t>(effect.navigation.value)}' in texts['IntervalSelectionActivity.cpp']
assert 'char stepText[24];' in texts['IntervalSelectionActivity.cpp']
message = texts['FullScreenMessageActivity.cpp']
assert 'XpUiCommand commands[2]{};' in message
assert 'paintRustScene(scene);' in message and 'finish()' not in message
assert 'void loop() override {}' in texts['FullScreenMessageActivity.h']
assert 'void render(RenderLock&&) override {}' in texts['FullScreenMessageActivity.h']
assert ': RustActivityAdapter("FullScreenMessage"' in texts['FullScreenMessageActivity.h']
cmake = (HERE / 'CMakeLists.txt').read_text()
assert not re.search(r'(?:include_directories|add_executable|add_library)[^)]*native_oracle', cmake, re.S)
print(f"8 native contracts, {sum(row['bytes'] for row in manifest['inputs'])} frozen bytes")

# Execute the exact native interception and scalar projection bodies. Unrelated
# hardware and constructors are replaced by counted objects, never screen policy.
import subprocess
import tempfile
ROOT = HERE.parents[1]
def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
projection = method('src/activities/util/DialogActivity.cpp', 'void xp_dialog_deliver(')
push = method('src/activities/ActivityManager.cpp', 'bool ActivityManager::pushActivity(')
source = r'''#include <cassert>
#include <memory>
#include <string>
#include <xp_dialog.h>
#include "ActivityResult.h"
int destroyed=0, pushed=0, legacySent=0, delivered=0;
bool accept=true;
void* transferred=nullptr;
class DialogActivity;
struct Activity {
  std::string name;
  Activity(std::string n):name(n){}
  virtual ~Activity(){++destroyed;}
  // BASE_HOOK
};
struct DialogActivity:Activity {
  using Activity::Activity;
  // DIALOG_HOOK
};
struct OwnedLegacyActivity { std::unique_ptr<Activity> activity; };
std::unique_ptr<OwnedLegacyActivity> ownLegacy(std::unique_ptr<Activity> a) {
  return a?std::make_unique<OwnedLegacyActivity>(std::move(a)):nullptr;
}
bool send(const XpAppEvent&){++legacySent;return false;}
extern "C" uint8_t xp_app_keyboard_push(void*){assert(false);return 0;}
extern "C" uint8_t xp_app_dialog_push(void* p){++pushed;if(accept)transferred=p;return accept;}
struct ActivityManager {bool pushActivity(std::unique_ptr<Activity>&&);};
// PUSH
extern "C" {
// PROJECTION
}
int main(){
 ActivityManager m;
 for(const char* name:{"SleepTimeoutInterval","custom interval","KeyboardEntry"}) {
  for(bool success:{false,true}) {
   accept=success;int old=destroyed, calls=pushed;
   m.pushActivity(std::make_unique<DialogActivity>(name));
   assert(pushed==calls+1 && legacySent==0);
   assert(destroyed==old+!success);
   if(success){delete static_cast<OwnedLegacyActivity*>(transferred);transferred=nullptr;}
   assert(destroyed==old+1);
  }
 }
 m.pushActivity(std::make_unique<Activity>("ordinary"));assert(legacySent==1);
 int calls=pushed;m.pushActivity(nullptr);assert(pushed==calls);
 XpAppLegacy services{};
 services.deliver_result=[](void* p,const void* raw){
  const auto& r=*static_cast<const ActivityResult*>(raw);
  const unsigned kind=*static_cast<unsigned*>(p);
  assert(r.isCancelled==(kind==0));
  if(kind==2)assert(std::get<IntervalResult>(r.data).value==UINT32_MAX);
  else assert(std::holds_alternative<std::monostate>(r.data));
  ++delivered;
 };
 for(unsigned kind=0;kind<4;++kind)xp_dialog_deliver(&services,&kind,kind,UINT32_MAX);
 assert(delivered==4);
}
'''
source = source.replace('// BASE_HOOK', method('src/activities/Activity.h', 'virtual DialogActivity* dialog()'))
source = source.replace('// DIALOG_HOOK', method('src/activities/util/DialogActivity.h', 'DialogActivity* dialog()'))
source = source.replace('// PUSH', push).replace('// PROJECTION', projection)
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'native.cpp').write_text(source)
    subprocess.run(['c++', '-std=c++20', '-UNDEBUG', '-I'+str(ROOT/'src/activities'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'), str(path/'native.cpp'),
                    '-o', str(path/'native')], check=True)
    subprocess.run([str(path/'native')], check=True)
print('native dialog interception: one transfer, rejected destruction, arbitrary names; four scalar result projections')
