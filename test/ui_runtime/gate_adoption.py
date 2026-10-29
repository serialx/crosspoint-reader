"""Production adapter/family recovery, exactly-once hooks, and stale publication."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('XP_REJECTION_SOURCE_ROOT', ROOT))

def method(path, name):
    text = (SOURCE / path).read_text()
    start = text.index(name)
    start = text.rfind('\n', 0, start) + 1
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

code = r'''
#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"
#include <xp_ui_settings.h>
#include <xp_ui_koreader.h>
#include <xp_ui_catalog.h>
#include <xp_ui_updates.h>
#include <cassert>
#include <memory>
#include <cstdio>
#define LOG_DBG(...) ((void)0)
#define tr(x) #x
static bool failGrowth=false;
static unsigned growthCalls=0;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(unsigned n) {
  if(n==64) {++growthCalls;if(failGrowth)return {};}
  return std::unique_ptr<T>(new typename std::remove_extent<T>::type[n]);
}
struct Rect {int x=0,y=0,width=480,height=800;};
struct Metrics {
  int listRowHeight=44,listRowGap=0,listInset=0,listSidePadding=0,listScrollWidth=5,listRowRadius=0,
      listSelectionStyle=0,listScrollSide=0,listTitleBold=0,topPadding=0,headerHeight=40,buttonHintsHeight=40,
      verticalSpacing=8,tabBarHeight=30,contentSidePadding=5;
};
struct UITheme {
  static UITheme& getInstance(){static UITheme t;return t;}
  Metrics getMetrics(){return {};}
  Rect getScreenSafeArea(GfxRenderer&,bool,bool){return {};}
  int getStatusBarHeight(){return 20;}
};
struct Fonts {int bodyFontId=1,smallFontId=0;};
Fonts uiScaleSpec(){return {};}
constexpr int UI_12_FONT_ID=1, UI_10_FONT_ID=0;
static uint8_t testView(const XpUiListState* s,const XpUiSettingsLayout*,const XpUiSettingsResources*,
 const XpUiBuffer* b,XpUiListState* next,XpUiScene* scene,XpUiSettingsHits* hits) {
 *next=*s;next->drawn=3;next->drawn_count=12;
 *scene={};*hits={};hits->first_row={0,0,100,20};hits->count=hits->uniform=1;
 return b->command_capacity<64?2:1;
}
struct SettingsUiActivity:RustActivityAdapter {
 using RustActivityAdapter::RustActivityAdapter;
 struct Device {int16_t width=480,height=800;struct {int16_t left=0,top=0,right=0,bottom=0;}safeArea;};
 struct Target {Device deviceContext(){return {};}}textTarget;
 XpUiSettingsKind kind=XP_UI_SETTINGS_SECTION;
 XpUiListState state{},next{};XpUiSettingsHits publishedHits{},nextHits{};XpUiSettingsLayout layout{};
 Publication publication;
 std::unique_ptr<XpUiCommand[]> commands;
 uint16_t commandCapacity=0,requestedCapacity=0;
 int renderingScroll=0,touchX=0,touchY=0;
 bool sceneFailed=false,touchActivated=false,longTouch=false;
 unsigned failures=0,activations=0,backs=0;
 int listCount()const{return 12;}void prepareRows(){}bool smallLabelsOnTouch(){return true;}
 uint8_t screenFlags(){return 0;}XpUiSettingsResources resources(){return {};}
 bool reserveCommands(uint16_t);bool buildRustScene(XpUiScene&)override;
 XpUiEffect updateRust(const XpUiInput&)override;void rustPublished()override;void rustSceneFailed()override;
 XpUiInterest rustInterest(uint32_t)const override;
 bool uiSceneFailed()const;bool routingReady()const;
 void loop()override;bool handleCustomInput(){return false;}
 void activateIndex(int){++activations;}void onRowLongPress(int){++activations;}void onBackButton(){++backs;}
 void applyRustEffect(const XpUiEffect&)override;
 Publication* rustPublication()override{return &publication;}
 bool rejected(){return rustSceneRejected();}
};
struct KOReaderUiActivity:RustActivityAdapter {
 using RustActivityAdapter::RustActivityAdapter;
 XpKoState model{},painting{};const char* status="";
 Publication publication;bool sceneFailed=false,touchDispatched=false,buildOk=true;
 unsigned failures=0,activations=0,backs=0;uint8_t nextHits=0;
 void publish(XpKoPhase,XpKoMessage,uint8_t,const char*);
 void markReturn();void rustSceneFailed()override;void rustPublished()override;
 XpUiInterest rustInterest(uint32_t)const override;XpUiEffect updateRust(const XpUiInput&)override;
 void applyRustEffect(const XpUiEffect&)override;
 void cancelRejectedInput(const XpUiInput&)override;
 bool buildRustScene(XpUiScene& s)override {painting=model;s={};static XpUiInteraction hit{{0,0,100,100},1,0,1,1,0};s.interactions=&hit;s.interaction_count=1;s.generation=1;return buildOk;}
 Publication* rustPublication()override{return &publication;}
 void performEffect(const XpUiEffect& e){if(e.kind==XP_UI_ACTIVATE)++activations;if(e.kind==XP_UI_CANCEL)++backs;}
 bool rejected(){return rustSceneRejected();}
};
'''
for name in ['reserveCommands','buildRustScene','updateRust','rustPublished','rustSceneFailed',
             'rustInterest','uiSceneFailed','routingReady','loop','applyRustEffect']:
    body = method('src/activities/settings/SettingsUiActivity.cpp', 'SettingsUiActivity::'+name+'(')
    if name == 'buildRustScene':
        body = body.replace('xp_ui_settings_view(', 'testView(')
    if name == 'rustSceneFailed':
        body = body.replace('{', '{ ++failures;', 1)
    code += '\n' + body
for name in ['publish','markReturn','rustSceneFailed','rustInterest','updateRust','applyRustEffect']:
    body = method('src/activities/reader/KOReaderUiActivity.cpp', 'KOReaderUiActivity::'+name+'(')
    if name == 'rustSceneFailed':
        body = body.replace('{', '{ ++failures;', 1)
    code += '\n' + body
header = (SOURCE/'src/activities/reader/KOReaderUiActivity.h').read_text()
if 'void rustPublished() override {' in header:
    body = method('src/activities/reader/KOReaderUiActivity.h','void rustPublished() override')
    code += '\n' + body.replace('void rustPublished() override', 'void KOReaderUiActivity::rustPublished()')
else:
    code += '\n' + method('src/activities/reader/KOReaderUiActivity.cpp','KOReaderUiActivity::rustPublished(')
ko = (SOURCE/'src/activities/reader/KOReaderUiActivity.cpp').read_text()
if 'KOReaderUiActivity::cancelRejectedInput(' in ko:
    code += '\n' + method('src/activities/reader/KOReaderUiActivity.cpp','KOReaderUiActivity::cancelRejectedInput(')
else:
    code += '\nvoid KOReaderUiActivity::cancelRejectedInput(const XpUiInput& i){RustActivityAdapter::cancelRejectedInput(i);}'
code += r'''
template<class T> void gestures(T& a,GfxRenderer& r,MappedInputManager& i,bool touch) {
 i={};if(touch)i.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;
 else i.pressed=i.held=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();r.paintOk=false;a.render(RenderLock{});assert(a.rejected());
 a.loop();r.paintOk=true;a.render(RenderLock{});assert(!a.rejected());
 i={};if(touch)i.touchFlags=XP_UI_TOUCH_RELEASED;
 else i.released=1u<<unsigned(MappedInputManager::Button::Confirm);
 auto before=a.activations;a.loop();assert(a.activations==before);
 i={};if(touch)i.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;
 else i.pressed=i.held=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();i={};if(touch)i.touchFlags=XP_UI_TOUCH_RELEASED;
 else i.released=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();assert(a.activations==before+1);
}
int main(int argc,char**argv){
 const std::string mode=argc>1?argv[1]:"all";
 if(mode=="all"||mode=="hooks")for(bool published:{false,true}) {
  GfxRenderer r;r.finishRefresh=true;MappedInputManager i;
  SettingsUiActivity a("settings",r,i);xp_ui_settings_list_init(&a.state,0);
  if(published)a.render(RenderLock{});
  a.commands.reset();a.commandCapacity=a.requestedCapacity=0;assert(a.reserveCommands(48));
  failGrowth=true;auto before=a.failures;auto calls=growthCalls;
  a.render(RenderLock{});assert(a.rejected());assert(a.failures==before+1);assert(growthCalls==calls+1);
  failGrowth=false;a.render(RenderLock{});assert(!a.rejected()&&a.commandCapacity==64);assert(growthCalls==calls+2);
 }
 if(mode=="all"||mode=="gestures")for(bool touch:{false,true})for(bool published:{false,true}) {
  GfxRenderer r;r.finishRefresh=true;MappedInputManager i;
  SettingsUiActivity s("settings",r,i);xp_ui_settings_list_init(&s.state,0);
  if(published)s.render(RenderLock{});gestures(s,r,i,touch);
  i={};KOReaderUiActivity k("koreader",r,i);k.model.phase=XP_KO_RESULT;
  if(published)k.render(RenderLock{});
  // The real KOReader update resolves a published row action through the adapter.
  static XpUiInteraction hit{{0,0,100,100},1,0,0,1};
  k.publication.scene.interactions=&hit;k.publication.scene.interaction_count=1;k.publication.ready=true;
  gestures(k,r,i,touch);
 }
 puts("gate adoption passed");
}
'''
with tempfile.TemporaryDirectory(prefix='gate-adoption-') as tmp:
    p = Path(tmp)
    for name in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:
        (p/name).write_text((SOURCE/'src/activities'/name).read_text())
    stub=(ROOT/'test/ui_runtime/stubs/Activity.h').read_text()
    stub=stub.replace('bool wasPressed(Button b)', 'bool hasTouch()const{return true;} bool wasScreenLongPress(int&,int&)const{return false;} bool wasPressed(Button b)')
    stub=stub.replace('XpNativeProvider textProvider()', 'int getScreenWidth()const{return 480;} int getScreenHeight()const{return 800;} XpNativeProvider textProvider()')
    (p/'Activity.h').write_text(stub)
    (p/'check.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-I'+str(p),'-I'+str(ROOT/'test/ui_runtime/stubs'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(p/'check.cpp'),str(p/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check'),*sys.argv[2:]],check=True)
