"""Run Catalog and Update custom renders through production input/rejection gates."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path(os.environ.get('XP_REJECTION_SOURCE_ROOT',ROOT))
def method(path,name):
 s=(SOURCE/path).read_text();start=s.index(name);start=s.rfind('\n',0,start)+1
 end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
code=r'''
#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"
#include <xp_ui_catalog.h>
#include <xp_ui_updates.h>
#include <cassert>
#include <cstdio>
#define LOG_DBG(...) ((void)0)
struct CatalogUiActivity:RustActivityAdapter {
 using RustActivityAdapter::RustActivityAdapter;
 struct ResourceLock{ResourceLock(CatalogUiActivity&){}};
 Publication publication;XpUiCatalogState ui{},paintState{};XpUiListState next{};
 XpUiCatalogHits publishedHits{},nextHits{};int consumedScroll=0;
 bool buildOk=true,touchActivated=false;unsigned activations=0,backs=0,failures=0;
 Publication* rustPublication()override{return &publication;}
 bool buildCatalogScene(XpUiScene& s){paintState=ui;next=ui.list;s={};nextHits={};
 nextHits.row_area={0,0,100,20};nextHits.rows=1;nextHits.heights[0]=20;return buildOk;}
 bool buildRustScene(XpUiScene&)override;void render(RenderLock&&)override;void loop()override;
 void rustSceneFailed()override;void rustPublished()override;void cancelRejectedInput(const XpUiInput&)override;
 XpUiEffect updateRust(const XpUiInput&)override;void applyRustEffect(const XpUiEffect&)override;
 XpUiInterest rustInterest(uint32_t)const override;
 bool handleCustomInput(){return false;}int listCount()const{return 3;}bool hasSearch()const{return false;}
 uint8_t pageLinks()const{return 0;}void activateIndex(int){++activations;}void onBackButton(){++backs;}
 void catalogEffect(const XpUiEffect& e){if(e.kind)++activations;}
 bool rejected(){return rustSceneRejected();}
};
struct UpdateUiActivity:RustActivityAdapter {
 using RustActivityAdapter::RustActivityAdapter;
 struct ResourceLock{ResourceLock(UpdateUiActivity&){}};
 Publication publication;XpUiUpdateState model{},painting{};XpUiUpdateFeedback feedback{},next{};
 XpUiUpdateEffect pending{};bool pendingHaptic=false,routesReady=false,buildOk=true;
 int inputRowCount=3,inputFamilyCount=3;unsigned activations=0,backs=0,failures=0;
 Publication* rustPublication()override{return &publication;}
 bool buildRustScene(XpUiScene& s)override{painting=model;s={};next={};
 next.rows.first_row={0,0,100,20};next.rows.count=next.rows.uniform=1;return buildOk;}
 void releaseText(){};void render(RenderLock&&)override;void loop()override;
 void rustSceneFailed()override;void rustPublished()override;void cancelRejectedInput(const XpUiInput&)override;
 XpUiEffect updateRust(const XpUiInput&)override;void applyRustEffect(const XpUiEffect&)override;
 XpUiInterest rustInterest(uint32_t)const override;
 int rowCount()const{return 3;}int familyCount()const{return 3;}
 void requestUpdate(bool=false){}
 void performAction(XpUiUpdateAction a,int32_t){if(a==XP_UI_UPDATE_FINISH)++backs;else if(a)++activations;}
 bool rejected(){return rustSceneRejected();}
};
'''
for name,path in [('CatalogUiActivity','src/activities/CatalogUiActivity.cpp'),('UpdateUiActivity','src/activities/settings/UpdateUiActivity.cpp')]:
 names=['render','loop','rustSceneFailed','rustPublished','cancelRejectedInput','updateRust','applyRustEffect','rustInterest']
 if name=='CatalogUiActivity':names+=['buildRustScene']
 for n in names:
  body=method(path,name+'::'+n+'(')
  if n=='rustSceneFailed':body=body.replace('{','{ ++failures;',1)
  code+='\n'+body
code+=r'''
template<class T>void check(T& a,GfxRenderer& r,MappedInputManager& i,bool published,bool build,bool touch){
 if(published)a.render(RenderLock{});
 i={};if(touch)i.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;
 else i.pressed=i.held=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();auto count=a.activations;
 a.buildOk=!build;r.paintOk=build;a.render(RenderLock{});
 assert(a.rejected()&&!a.publication.ready&&a.failures==1);
 a.loop();a.buildOk=r.paintOk=true;a.render(RenderLock{});assert(!a.rejected());
 i={};if(touch)i.touchFlags=XP_UI_TOUCH_RELEASED;
 else i.released=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();assert(a.activations==count);
 i={};if(touch)i.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;
 else i.pressed=i.held=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();i={};if(touch)i.touchFlags=XP_UI_TOUCH_RELEASED;
 else i.released=1u<<unsigned(MappedInputManager::Button::Confirm);
 a.loop();assert(a.activations==count+1);
 a.buildOk=false;a.render(RenderLock{});i={};i.released=1u<<unsigned(MappedInputManager::Button::Back);
 a.loop();assert(a.backs==1);
}
int main(){
 for(bool published:{false,true})for(bool build:{false,true})for(bool touch:{false,true}){
 GfxRenderer r;r.finishRefresh=true;MappedInputManager i;
 CatalogUiActivity c("catalog",r,i);xp_ui_catalog_init(&c.ui,XP_UI_CATALOG_SERVERS,0,0);
 check(c,r,i,published,build,touch);
 r.paintOk=true;i={};UpdateUiActivity u("updates",r,i);xp_ui_updates_init(&u.model,XP_UI_UPDATE_FONTS,0);u.model.phase=3;
 check(u,r,i,published,build,touch);
}
for(int kind:{XP_UI_UPDATE_OTA,XP_UI_UPDATE_CACHE})for(bool published:{false,true})for(bool build:{false,true}){
 GfxRenderer r;r.finishRefresh=true;MappedInputManager i;UpdateUiActivity u("popup",r,i);
 xp_ui_updates_init(&u.model,kind,0);u.model.phase=kind==XP_UI_UPDATE_OTA?2:0;u.model.popup_active=1;u.model.popup_selected=1;
 if(published)u.render(RenderLock{});u.buildOk=!build;r.paintOk=build;u.render(RenderLock{});
 i.released=1u<<unsigned(MappedInputManager::Button::Confirm);u.loop();assert(u.activations==0&&u.model.popup_active);
 i.released=1u<<unsigned(MappedInputManager::Button::Back);u.loop();assert(!u.model.popup_active&&!u.activations);
 if(kind==XP_UI_UPDATE_CACHE)assert(u.backs==1);
 else {assert(u.backs==0);i={};u.loop();assert(u.backs==1);}
}
puts("custom gate build/paint failures, Back, held button/touch and recovery passed");}
'''
with tempfile.TemporaryDirectory(prefix='custom-gates-')as tmp:
 p=Path(tmp)
 for n in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:(p/n).write_text((SOURCE/'src/activities'/n).read_text())
 stub=(ROOT/'test/ui_runtime/stubs/Activity.h').read_text().replace('bool wasPressed(Button b)','bool wasScreenLongPress(int&,int&)const{return false;} bool wasPressed(Button b)')
 (p/'Activity.h').write_text(stub);(p/'check.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-I'+str(p),'-I'+str(ROOT/'test/ui_runtime/stubs'),'-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(p/'check.cpp'),str(p/'RustActivityAdapter.cpp'),str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
