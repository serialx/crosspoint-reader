"""Fault tests for native remap and BLE actions that bypass adapter updateRust."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path(os.environ.get('XP_REJECTION_SOURCE_ROOT',ROOT))

def method(path,signature):
    s=(SOURCE/path).read_text();a=s.index(signature);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]

code=r'''
#include "RustActivityAdapter.h"
#include <xp_ui_settings.h>
#include <cassert>
struct CrossPointSettings { enum {FRONT_HW_BACK,FRONT_HW_CONFIRM,FRONT_HW_LEFT,FRONT_HW_RIGHT}; };
struct Settings {
 int frontButtonBack=0,frontButtonConfirm=1,frontButtonLeft=2,frontButtonRight=3;
 struct {bool enabled=false;} ble;
 unsigned writes=0;
 void saveToFile(){++writes;}
} SETTINGS;
namespace bleturner {bool service(){return false;}}
class SettingsUiActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 bool buildOk=true;
 unsigned finished=0;
 void finish(){++finished;}
 void selectIndex(int){}
 int selectedIndex(){return 0;}
 bool buildRustScene(XpUiScene&) override{return buildOk;}
 void applyRustEffect(const XpUiEffect& effect) override{if(effect.kind==XP_UI_CANCEL)finish();}
};
class ButtonRemapActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 XpUiSettingsRemap remap{};
 void loop() override;
 void cancelRejectedInput(const XpUiInput&) override;
};
class BleSettingsActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 XpUiSettingsBle state{};
 unsigned cleared=0,polled=0;
 void loop() override;
 void clearMapping(int){++cleared;}
 void stepLearning(){++polled;}
 uint32_t signature(){return state.shown_signature;}
 void refresh(){}
};
'''
code+=method('src/activities/settings/ButtonRemapActivity.cpp','void ButtonRemapActivity::loop()')
inline=method('src/activities/settings/ButtonRemapActivity.h','  void cancelRejectedInput(')
code+=inline.replace('  void cancelRejectedInput(', 'void ButtonRemapActivity::cancelRejectedInput(').replace(' override','')
code+=method('src/activities/settings/BleSettingsActivity.cpp','void BleSettingsActivity::loop()')
code+=r'''
int main(){
 for(bool first:{false,true})for(bool buildFailure:{false,true}){
  GfxRenderer renderer;MappedInputManager input;
  ButtonRemapActivity remap("remap",renderer,input);
  xp_ui_settings_remap_init(&remap.remap);
  const auto paint=[&](auto& a){RenderLock lock;a.render(std::move(lock));};
  if(first)paint(remap);
  remap.buildOk=!buildFailure;renderer.paintOk=buildFailure;paint(remap);
  unsigned writes=SETTINGS.writes;
  for(int raw=0;raw<4;++raw){input.rawPressed=raw;remap.loop();}
  input.pressed=1u<<unsigned(MappedInputManager::Button::Up);remap.loop();
  assert(SETTINGS.writes==writes && remap.remap.step==0 && remap.finished==0);
  input.pressed=1u<<unsigned(MappedInputManager::Button::Back);remap.loop();
  assert(remap.finished==0);
  input.pressed=1u<<unsigned(MappedInputManager::Button::Down);remap.loop();
  assert(remap.finished==1 && SETTINGS.writes==writes);
  input={};remap.buildOk=renderer.paintOk=true;paint(remap);
  input.pressed=1u<<unsigned(MappedInputManager::Button::Up);remap.loop();
  assert(SETTINGS.writes==writes+1);
  input={};
  BleSettingsActivity ble("ble",renderer,input);ble.state.view=XP_UI_SETTINGS_BLE_VIEW_MAPPING;
  if(first)paint(ble);
  ble.buildOk=!buildFailure;renderer.paintOk=buildFailure;paint(ble);
  input.longConfirm=true;input.held=1u<<unsigned(MappedInputManager::Button::Confirm);ble.loop();
  assert(ble.cleared==0);
  ble.buildOk=renderer.paintOk=true;paint(ble);ble.loop();assert(ble.cleared==0);
  input={};input.released=1u<<unsigned(MappedInputManager::Button::Confirm);ble.loop();assert(ble.cleared==0);
  input={};input.longConfirm=true;ble.loop();assert(ble.cleared==1);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    out=Path(directory);(out/'bypass.cpp').write_text(code)
    for name in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:
        (out/name).write_bytes((SOURCE/'src/activities'/name).read_bytes())
    subprocess.run(['c++','-std=c++20','-pthread','-UNDEBUG','-I'+str(out),'-I'+str(ROOT/'test/ui_runtime/stubs'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(out/'bypass.cpp'),str(out/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(out/'bypass')],check=True)
    subprocess.run([str(out/'bypass')],check=True,timeout=30)
print('Native remap and BLE failure/recovery paths pass')
