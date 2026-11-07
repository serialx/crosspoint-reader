"""Execute frozen root lifecycle methods with physical/child allocation fakes.

Snapshots are data: their includes are stripped before a generated translation
unit is compiled. The live settings base remains in the firmware.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FROZEN = HERE / 'native_oracle'


def stripped(name):
    return re.sub(r'^\s*#(?:include|pragma).*$', '', (FROZEN / name).read_text(), flags=re.M)


def record(mutate=False):
    section = stripped('SettingsSection.h.txt')
    header = stripped('SettingsListActivity.h.txt').replace(
        'final : public SettingsUiActivity {', 'final : public SettingsUiActivity {\n public:')
    source = stripped('SettingsListActivity.cpp.txt')
    if mutate:
        source = source.replace('    openSection(section);', '    openSection(SettingsSection::General);')
    methods = stripped('SettingsUiActivity.methods.cpp.txt')
    prefix = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <xp_ui_settings.h>
namespace freeink::ui {}
struct GfxRenderer {}; struct MappedInputManager {};
enum class StrId { STR_SETTINGS_GENERAL, STR_CAT_DISPLAY, STR_CAT_READER,
 STR_CAT_CONTROLS, STR_LIBRARY, STR_SETTINGS_NETWORK, STR_SETTINGS_TITLE };
constexpr auto STR_SETTINGS_TITLE=StrId::STR_SETTINGS_TITLE;
static bool fail=false, changed=false;
static int updates=0, homes=0, reloads=0, enters=0, exits=0, resets=0, errors=0;
static std::vector<int> sections;
static std::function<void()> returned;
static std::function<void(size_t)> allocating;
struct Translation { const char* get(StrId) {return changed?"Changed":"Setting";} } I18N;
const char* tr(StrId id) {return I18N.get(id);}
#define CROSSPOINT_VERSION "oracle-version"
#define LOG_ERR(...) (++errors)
template<class T, class... Args> auto makeUniqueNoThrow(Args&&... args) {
 if constexpr (std::is_array_v<T>) {if(allocating) allocating((args,...));}
 if(std::exchange(fail,false)) return std::unique_ptr<T>{};
 return std::make_unique<T>(std::forward<Args>(args)...);
}
struct UITheme {static UITheme& getInstance() {static UITheme t;return t;} void reload(){++reloads;}};
struct ActivityResult {};
struct Activity {
 virtual ~Activity()=default;
 virtual void onEnter(){++enters;} virtual void onExit(){++exits;} virtual void loop(){}
};
struct SettingsUiRow {const char* label=nullptr;uint32_t icon=0;int16_t actionValue=0;};
'''
    base = r'''
struct SettingsActivity : Activity {
 SettingsActivity(GfxRenderer&,MappedInputManager&,SettingsSection s){sections.push_back(static_cast<int>(s));}
};
struct SettingsUiActivity : Activity {
 GfxRenderer& renderer; MappedInputManager& mappedInput; XpUiSettingsKind kind;
 XpUiListState state{}; struct {bool ready=false;} publication;
 std::unique_ptr<XpUiCommand[]> commands; uint16_t commandCapacity=0;
 SettingsUiActivity(const char*,GfxRenderer& r,MappedInputManager& i,XpUiSettingsKind k):renderer(r),mappedInput(i),kind(k){}
 bool reserveCommands(uint16_t);void onEnter()override;void onExit()override;
 virtual int listCount()const=0;virtual const SettingsUiRow* rowAt(int)const=0;
 virtual void prepareRows()=0;virtual void activateIndex(int)=0;virtual void onBackButton()=0;
 virtual const char* headerTitle()const=0;virtual const char* headerTrailing()const=0;
 bool hasUiStorage()const{return commands!=nullptr;}
 void selectIndex(int index){state.selected=index;}
 void requestUpdate(){++updates;}void resetUi(){++resets;publication.ready=false;}
 void onGoHome(){++homes;}
 template<class F>void startActivityForResult(std::unique_ptr<SettingsActivity> a,F f){
  assert(a);returned=[f]{f(ActivityResult{});};
 }
};
extern "C" void xp_ui_settings_list_init(XpUiListState* s,int32_t selected){*s={};s->selected=selected;}
'''
    main = r'''
int main(){
 GfxRenderer r;MappedInputManager i;
 for(int initial=0;initial<=6;++initial){
  sections.clear();changed=false;
  SettingsListActivity root(r,i,static_cast<SettingsSection>(initial));
  root.onEnter();assert(root.commandCapacity==32);assert(sections.empty());
  assert(root.listCount()==6);
  for(int n=0;n<6;++n){assert(root.rowAt(n)->icon==static_cast<unsigned>(n+1));assert(root.rowAt(n)->actionValue==n);}
  root.loop();assert(sections.size()==static_cast<size_t>(initial<6));
  if(initial<6){assert(sections.back()==initial);assert(root.state.selected==initial);
   changed=true;root.publication.ready=true;returned();returned={};
   assert(!root.publication.ready);assert(!strcmp(root.rowAt(0)->label,"Changed"));}
  root.onExit();assert(!root.commands);assert(root.commandCapacity==0);
 }
 assert(enters==7&&exits==7&&reloads==7&&resets==6);
 SettingsListActivity root(r,i,SettingsSection::Network);
 root.onEnter();fail=true;root.loop();assert(root.state.selected==5);assert(errors==1);
 assert(!strcmp(root.headerTrailing(),"oracle-version"));root.onBackButton();assert(homes==1);root.onExit();
 for(int failure=32;failure<=128;failure+=16){
  SettingsListActivity storage(r,i);
  for(int n=32;n<=128;n+=16){
   allocating=[&](size_t count){assert(!storage.commands);assert(count==storage.commandCapacity);};
   fail=n==failure;assert(storage.reserveCommands(n)==(n!=failure));
   assert(storage.commandCapacity==n);
   if(n==failure){assert(!storage.commands);assert(storage.reserveCommands(storage.commandCapacity));}
  }
  auto old=storage.commands.get();assert(!storage.reserveCommands(144));
  assert(storage.commands.get()==old&&storage.commandCapacity==128);allocating={};
 }
 puts("six sections; End; deferred Network; failed child selection; changed labels; exit reload; exact arena retries");
}
'''
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / 'root.cpp'
        cpp.write_text(prefix + section + base + header + methods + source + main)
        binary = Path(directory) / 'root'
        subprocess.run(['c++', '-std=c++20', '-I' + str(ROOT / 'rust/crates/xp-ffi/include'),
                        str(cpp), '-o', str(binary)], check=True)
        return subprocess.run([str(binary)], capture_output=True)


class NativeRoot(unittest.TestCase):
    def test_snapshot_manifest(self):
        manifest = json.loads((FROZEN / 'manifest.json').read_text())
        for entry in manifest['files']:
            data = (FROZEN / entry['file']).read_bytes()
            self.assertEqual(len(data), entry['bytes'])
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])

    def test_lifecycle_and_allocation_contract(self):
        result = record()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b'six sections; End; deferred Network; failed child selection; changed labels; exit reload; exact arena retries\n')

    def test_pending_section_mutation(self):
        self.assertNotEqual(record(True).returncode, 0)


if __name__ == '__main__':
    unittest.main()
