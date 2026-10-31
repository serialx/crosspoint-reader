"""Deterministic production input paths that wait across scene rejection."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("XP_REJECTION_SOURCE_ROOT", ROOT))


def method(path, signature):
    source = (SOURCE / path).read_text()
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


code = r'''
#include "RustActivityAdapter.h"
#include <xp_ui_settings.h>
#include <xp_ui_reader_chrome.h>
#include <xp_ui_dictionary.h>
#include <xp_ui_text_settings.h>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
struct CrossPointSettings { enum {FRONT_HW_BACK,FRONT_HW_CONFIRM,FRONT_HW_LEFT,FRONT_HW_RIGHT}; };
struct Settings {
 int frontButtonBack=3,frontButtonConfirm=2,frontButtonLeft=1,frontButtonRight=0;
 int fontFamily=0,fontPointSize=12,language=0;
 char sdFontFamilyName[16]{};
 unsigned writes=0;
 void saveToFile(){++writes;}
} SETTINGS;
struct ActivityResult {bool isCancelled=false;};
class SettingsUiActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 bool buildOk=true;
 unsigned finished=0;
 void finish(){++finished;}
 void selectIndex(int){}
 bool buildRustScene(XpUiScene&) override{return buildOk;}
 void applyRustEffect(const XpUiEffect& e) override{if(e.kind==XP_UI_CANCEL)finish();}
};
class ButtonRemapActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 XpUiSettingsRemap remap{};
 void loop() override;
 void cancelRejectedInput(const XpUiInput&) override;
};
class EpubReaderFootnoteSelectActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 XpUiReaderFootnoteState state{};
 int footnoteLinkCount=1;
 unsigned jumps=0;
 void performJump(int){++jumps;finish();}
 void setResult(ActivityResult){}
 XpUiInterest rustInterest(uint32_t now) const override{return xp_ui_reader_footnote_interest(&state,now);}
 XpUiEffect updateRust(const XpUiInput&) override;
 void applyRustEffect(const XpUiEffect&) override;
};
'''
code += method('src/activities/settings/ButtonRemapActivity.cpp', 'void ButtonRemapActivity::loop()')
code += method('src/activities/settings/ButtonRemapActivity.h', '  void cancelRejectedInput(').replace(
    '  void cancelRejectedInput(', 'void ButtonRemapActivity::cancelRejectedInput(').replace(' override', '')
for signature in ['XpUiEffect EpubReaderFootnoteSelectActivity::updateRust(',
                  'void EpubReaderFootnoteSelectActivity::applyRustEffect(']:
    code += method('src/activities/reader/EpubReaderFootnoteSelectActivity.cpp', signature)
code += r'''
enum class Language {English,French};
constexpr uint8_t SORTED_LANGUAGE_INDICES[]={0,1};
struct {unsigned changes=0;void setLanguage(Language){++changes;} int getLanguage(){return 0;}} I18N;
class LanguageSelectActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 void activateIndex(int);
 void loop() override{if(!consumeRejectedInput())activateIndex(1);}
};
struct {unsigned loads=0;void ensureLoaded(GfxRenderer&){++loads;}} sdFontSystem;
XpUiTextValues sampleValues(){return {};}
constexpr int Settings::*FIELDS[]={&Settings::fontFamily};
class TextSettingsActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 XpUiTextState state{};
 struct Font {std::string name;bool isBuiltin;uint8_t settingIndex;};
 struct Size {uint8_t pointSize;};
 std::vector<Font> fonts_{{"first",true,0},{"second",true,1}};
 std::vector<Size> sizes_{{12},{18}};
 struct Popup {template<class F>void show(const char*,const char**,int,int,F){}} optionPopup_;
 int rebuildSizeList(){return 0;}
 void activateRow(int);
 void loop() override{if(!consumeRejectedInput())activateRow(1);}
'''
# Both revisions are compiled without adapting their production return contract.
for name in ['Family','Size']:
    prefix = 'bool' if ('bool TextSettingsActivity::apply'+name) in (SOURCE/'src/activities/settings/TextSettingsActivity.cpp').read_text() else 'void'
    code += f' {prefix} apply{name}(int);\n'
code += '};\n'
code += method('src/activities/settings/LanguageSelectActivity.cpp', 'void LanguageSelectActivity::activateIndex(')
for name in ['Family','Size']:
    prefix = 'bool' if ('bool TextSettingsActivity::apply'+name) in (SOURCE/'src/activities/settings/TextSettingsActivity.cpp').read_text() else 'void'
    code += method('src/activities/settings/TextSettingsActivity.cpp', prefix+' TextSettingsActivity::apply'+name+'(')
code += method('src/activities/settings/TextSettingsActivity.cpp', 'void TextSettingsActivity::activateRow(')
code += method('src/activities/reader/DictionaryUiBindings.cpp', 'XpUiInput dictionaryUiInput(')
code += r'''
class ClipSelectionActivity:public SettingsUiActivity {
 public:
 using SettingsUiActivity::SettingsUiActivity;
 XpUiSelectionState state{};
 XpUiSelectionPainted painted{};
 XpUiSelectionLayout layout{};
 XpUiSelectionWord word{};
 unsigned selections=0,cancellations=0,activations=0;
 int wordCount=1;
 struct Popup {
  unsigned inputs=0,activations=0;
  bool active=false;
  bool isActive()const{return active;}
  template<class F> bool handleInput(MappedInputManager& input,F){
   ++inputs;
   if(active && (input.touchFlags&XP_UI_TOUCH_RELEASED)){++activations;return true;}
   return false;
  }
  template<class F> bool handleButtons(bool,bool,bool,bool,F){return false;}
 } actionPopup;
 void setup(){
  word.x=0;word.y=0;word.width=100;word.height=20;word.text="word";
  layout.screen=layout.safe={0,0,600,800};layout.line_height=20;layout.touch=1;
  xp_ui_selection_init(&state,0,&word,1,600,-1,-1);
 }
 const XpUiSelectionWord* wireWords(){return &word;}
 void cancel(){++cancellations;}
 void loopButtons(){}
 void drainRejectedTouch();
 void applySelection(const XpUiSelectionUpdate& e){
  if(e.kind==XP_UI_SELECT_CANCEL)cancel();
  if(e.kind==XP_UI_SELECT_CONFIRM)++activations;
  if(e.invalidate && state.range_start==0 && state.dragging)++selections;
 }
 void loop() override;
 void rustSceneFailed() override;
 void cancelRejectedInput(const XpUiInput&) override;
};
'''
code += method('src/activities/reader/ClipSelectionActivity.cpp', 'void ClipSelectionActivity::loop()')
code += method('src/activities/reader/ClipSelectionActivity.cpp', 'void ClipSelectionActivity::drainRejectedTouch()')
for name in ['rustSceneFailed', 'cancelRejectedInput']:
    code += method('src/activities/reader/ClipSelectionActivity.h', '  void '+name+'(').replace(
        '  void '+name+'(', 'void ClipSelectionActivity::'+name+'(').replace(' override', '')
code += r'''
template<class T> void paint(T& a){RenderLock lock;a.render(std::move(lock));}
// The constructor notification is before lock(), after the production first gate.
// Input stays immutable until join: no scheduling delay or shared-input data race.
template<class T> void rejectWhileWaiting(T& a,GfxRenderer& r,bool buildFailure){
 RenderLock held;
 waitingForRender=false;
 std::thread input([&]{a.loop();});
 {std::unique_lock wait(waitMutex);assert(waitCv.wait_for(wait,std::chrono::seconds(5),[]{return waitingForRender;}));}
 a.buildOk=!buildFailure;r.paintOk=buildFailure;
 // We own the simulated render task's lock; invoke the production render path.
 a.render(std::move(held));
 held.unlock();input.join();
 assert(a.requested>0);
}
int main(int argc,char** argv){
 assert(argc==5);
 for(bool first:{false,true})for(bool buildFailure:{false,true}){
  if(first!=bool(atoi(argv[2])) || buildFailure!=bool(atoi(argv[3])))continue;
  if(std::string(argv[1])=="remap")for(int action:{0,1,2}){
   if(action!=atoi(argv[4]))continue;
   GfxRenderer r;MappedInputManager input;ButtonRemapActivity a("remap",r,input);
   xp_ui_settings_remap_init(&a.remap);
   if(first)paint(a);
   a.remap.step=3;a.remap.mapping[0]=0;a.remap.mapping[1]=1;a.remap.mapping[2]=2;
   const auto before=a.remap;const auto settings=SETTINGS;
   if(action==0)input.pressed=1u<<unsigned(MappedInputManager::Button::Up);
   if(action==1)input.rawPressed=3;
   if(action==2)input.pressed=1u<<unsigned(MappedInputManager::Button::Down);
   rejectWhileWaiting(a,r,buildFailure);
   assert(memcmp(&a.remap,&before,sizeof(before))==0);
   assert(SETTINGS.writes==settings.writes);
   assert(SETTINGS.frontButtonBack==settings.frontButtonBack &&
          SETTINGS.frontButtonConfirm==settings.frontButtonConfirm &&
          SETTINGS.frontButtonLeft==settings.frontButtonLeft &&
          SETTINGS.frontButtonRight==settings.frontButtonRight);
   assert(a.finished==unsigned(action==2));
   input={};a.buildOk=r.paintOk=true;paint(a);
   if(action==0)input.pressed=1u<<unsigned(MappedInputManager::Button::Up);
   else input.rawPressed=3;
   a.loop();assert(SETTINGS.writes==settings.writes+1);
   assert(a.finished==unsigned(action==2)+1);
  }
  if(std::string(argv[1])=="clip-lock"){
   GfxRenderer r;MappedInputManager input;ClipSelectionActivity a("clip",r,input);a.setup();
   if(first)paint(a);
   input.touchFlags=XP_UI_TOUCH_RELEASED;a.actionPopup.active=true;
   rejectWhileWaiting(a,r,buildFailure);
   assert(a.state.range_start==-1 && !a.selections && !a.activations && !a.actionPopup.activations);
   input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
   input={};a.actionPopup.active=false;a.buildOk=r.paintOk=true;paint(a);a.loop();
   input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
   assert(a.state.range_start==0 && a.selections==1);
  }
  if(std::string(argv[1])=="clip-entry-lock"){
   const int contact=atoi(argv[4]); // No contact, release edge, or held entry contact.
   GfxRenderer r;MappedInputManager input;ClipSelectionActivity a("clip",r,input);a.setup();
   a.state.ignore_touch=1;
   if(first)paint(a);
   input.touchFlags=contact==2?XP_UI_TOUCH_HELD:contact==1?XP_UI_TOUCH_RELEASED:0;
   rejectWhileWaiting(a,r,buildFailure);
   assert(a.state.ignore_touch==unsigned(contact==2));
   assert(a.state.range_start==-1 && !a.selections && !a.activations && !a.cancellations);
   // Publish recovery before any further input, with no idle or duplicate release.
   a.buildOk=r.paintOk=true;paint(a);
   if(contact==2){
    a.loop();assert(a.state.ignore_touch && !a.selections);
    input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
    assert(!a.state.ignore_touch && !a.selections && !a.activations && !a.cancellations);
   }
   input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
   assert(a.state.range_start==0 && a.selections==1);
   input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
   assert(a.selections==1 && !a.activations && !a.cancellations && !a.state.dragging);
  }
  if(std::string(argv[1])=="clip-touch")for(bool contact:{false,true}){
   if(contact!=bool(atoi(argv[4])))continue;
   GfxRenderer r;MappedInputManager input;ClipSelectionActivity a("clip",r,input);a.setup();
   if(first)paint(a);
   // An inherited contact is ignored on entry until its release.
   a.state.ignore_touch=1;
   input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
   assert(a.state.range_start==-1 && !a.selections);
   input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
   assert(!a.state.ignore_touch && a.state.range_start==-1);
   // Track the old contact without selecting: this is the entry gesture.
   if(contact){a.state.ignore_touch=1;input.touchFlags=XP_UI_TOUCH_HELD;a.loop();}
   else {input={};a.loop();}
   a.buildOk=!buildFailure;r.paintOk=buildFailure;paint(a);
   a.buildOk=r.paintOk=true;paint(a);
   if(contact){input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();assert(!a.selections);}
   // No idle frame is allowed between recovery/old release and the fresh press.
   input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
   if(!contact){
    // Recovery preceded the first rejection observation: consume this one
    // fresh gesture conservatively, then admit the next complete gesture.
    assert(a.state.range_start==-1 && !a.selections);
    input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
    assert(!a.selections && !a.activations && !a.cancellations);
    input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
   }
   assert(a.state.range_start==0 && a.selections==1);
   input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
   assert(a.selections==1 && !a.cancellations && !a.activations && !a.state.dragging);
  }
  if(std::string(argv[1])=="clip-entry"){
   GfxRenderer r;MappedInputManager input;ClipSelectionActivity a("clip",r,input);a.setup();
   a.state.ignore_touch=1; // onEnter owns this flag before the first input sample.
   if(first)paint(a);
   a.buildOk=!buildFailure;r.paintOk=buildFailure;paint(a);
   a.buildOk=r.paintOk=true;paint(a);
   input.touchFlags=XP_UI_TOUCH_HELD;a.loop();
   assert(a.state.range_start==-1 && !a.selections);
   input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
   assert(a.state.range_start==-1 && !a.selections);
   input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
   assert(a.state.range_start==0 && a.selections==1);
  }
  if(std::string(argv[1])=="language"){
   GfxRenderer r;MappedInputManager input;LanguageSelectActivity a("language",r,input);
   if(first)paint(a);
   const auto writes=SETTINGS.writes,changes=I18N.changes;
   rejectWhileWaiting(a,r,buildFailure);
   assert(SETTINGS.writes==writes && I18N.changes==changes && !a.finished);
   a.buildOk=r.paintOk=true;paint(a);a.loop();
   assert(SETTINGS.writes==writes+1 && I18N.changes==changes+1 && a.finished==1);
  }
  if(std::string(argv[1])=="text"){
   GfxRenderer r;MappedInputManager input;TextSettingsActivity a("text",r,input);
   xp_ui_text_init(&a.state,atoi(argv[4]),0,0,2,2,0);
   if(first)paint(a);
   const auto before=SETTINGS;const auto loads=sdFontSystem.loads;
   rejectWhileWaiting(a,r,buildFailure);
   assert(SETTINGS.writes==before.writes && SETTINGS.fontFamily==before.fontFamily &&
          SETTINGS.fontPointSize==before.fontPointSize && sdFontSystem.loads==loads);
   a.buildOk=r.paintOk=true;paint(a);a.loop();
   assert(SETTINGS.writes==before.writes+1 && sdFontSystem.loads==loads+1);
  }
  if(std::string(argv[1])=="footnote"){
   GfxRenderer r;MappedInputManager input;EpubReaderFootnoteSelectActivity a("footnote",r,input);
   if(first)paint(a);
   input.released=1u<<unsigned(MappedInputManager::Button::Confirm);
   rejectWhileWaiting(a,r,buildFailure);
   assert(a.jumps==0 && a.finished==0);
   input={};a.buildOk=r.paintOk=true;paint(a);
   input.released=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
   // No loop observed the rejection before recovery; this first release is
   // consumed even if it belongs to a fresh gesture.
   assert(a.jumps==0 && a.finished==0);
   input={};a.loop();
   input.released=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
   assert(a.jumps==1 && a.finished==1);
   input={};a.buildOk=false;paint(a);
   input.released=1u<<unsigned(MappedInputManager::Button::Back);a.loop();
   assert(a.jumps==1 && a.finished==2);
  }
 }
 puts("Production lock interleaving, cancel and recovery pass");
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    for name in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        (out / name).write_bytes((SOURCE / 'src/activities' / name).read_bytes())
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('class Activity;', """inline std::mutex waitMutex;
inline std::condition_variable waitCv;
inline bool waitingForRender=false;
class Activity;""")
    stub = stub.replace('RenderLock() { renderMutex.lock(); }', """RenderLock() {
      {std::lock_guard wait(waitMutex);waitingForRender=true;} waitCv.notify_all();
      renderMutex.lock();
    }""")
    stub = stub.replace('~RenderLock() { renderMutex.unlock(); }', 'bool owns=true; void unlock(){renderMutex.unlock();owns=false;} ~RenderLock() { if(owns)renderMutex.unlock(); }')
    stub = stub.replace('  SwipeDir swipe', """  bool hasTouch()const{return true;}
  bool isScreenTouchHeld(int& x,int& y)const{x=y=10;return touchFlags&XP_UI_TOUCH_HELD;}
  bool wasScreenTouchPressed(int& x,int& y)const{x=y=10;return touchFlags&XP_UI_TOUCH_PRESSED;}
  bool wasScreenTapped(int& x,int& y)const{x=y=10;return touchFlags&XP_UI_TOUCH_RELEASED;}
  SwipeDir swipe""")
    stub = stub.replace('void requestUpdate() {}', 'unsigned requested=0; void requestUpdate() {++requested;}')
    (out / 'Activity.h').write_text(stub)
    (out / 'interleaving.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'),
                    str(out/'interleaving.cpp'), str(out/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1], '-o', str(out/'interleaving')], check=True)
    results = []
    for case in (sys.argv[2:] or ['remap', 'footnote', 'clip-lock', 'clip-touch', 'clip-entry', 'clip-entry-lock', 'language', 'text']):
        for first in [0, 1]:
            for build_failure in [0, 1]:
                for action in (range(3) if case in ['remap', 'clip-entry-lock'] else range(2) if case in ['clip-touch', 'text'] else range(1)):
                    label = f'{case}: published={first}, build_failure={build_failure}, action/contact={action}'
                    result = subprocess.run([str(out/'interleaving'), case, str(first), str(build_failure), str(action)], timeout=30)
                    print(label, 'exit='+str(result.returncode), flush=True)
                    results.append(result.returncode)
    if any(results):
        raise SystemExit(1)
