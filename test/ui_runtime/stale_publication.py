"""Stale scenes cannot publish routes or recover rejection; fresh input still acts."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('XP_REJECTION_SOURCE_ROOT', ROOT))


def method(path, signature):
    source = (SOURCE / path).read_text()
    start = source.index(signature)
    start = source.rfind('\n', 0, start) + 1
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


# Service/resource leaves are fixed; publication, interest and update are production methods.
families = {
    'SettingsUiActivity': {
        'folder': 'settings',
        'fields': '''XpUiListState state{},next{}; int renderingScroll=0;
 XpUiSettingsHits publishedHits{},nextHits{}; bool longTouch=false,touchActivated=false;
 int touchX=0,touchY=0; int listCount()const{return 3;}''',
        'init': 'state.navigation_revision=1;',
        'snapshot': 'next=state;',
        'stale': ['next.navigation_revision=0;'],
    },
    'KOReaderUiActivity': {
        'folder': 'reader',
        'fields': '''XpKoState model{},painting{}; uint8_t nextHits=0; bool touchDispatched=false;''',
        'init': 'model.phase=XP_KO_RESULT;',
        'snapshot': 'painting=model;',
        'stale': ['painting.phase=XP_KO_WIFI;'],
    },
    'UpdateUiActivity': {
        'folder': 'settings',
        'fields': '''XpUiUpdateState model{},painting{}; XpUiUpdateFeedback feedback{},next{};
 XpUiUpdateEffect pending{}; bool pendingHaptic=false;
 int inputRowCount=3,inputFamilyCount=3;''',
        'init': 'xp_ui_updates_init(&model,XP_UI_UPDATE_FONTS,0);model.phase=3;',
        'snapshot': 'painting=model;next.list=model.list;',
        'stale': ['painting.phase=2;', 'painting.popup_active=1;'],
        'effect': '''if(pending.action){++actions;lastKind=pending.action;lastValue=pending.value;}''',
        'kind': 'XP_UI_UPDATE_FONT_ACTIVATE',
    },
    'CatalogUiActivity': {
        'folder': '',
        'fields': '''XpUiCatalogState ui{},paintState{}; XpUiListState next{};
 XpUiCatalogHits publishedHits{},nextHits{}; int consumedScroll=0; bool touchActivated=false;
 int listCount()const{return 3;} bool hasSearch()const{return false;} uint8_t pageLinks()const{return 0;}''',
        'init': 'xp_ui_catalog_init(&ui,XP_UI_CATALOG_SERVERS,0,0);',
        'snapshot': 'paintState=ui;next=ui.list;',
        'stale': ['paintState.mode=XP_UI_CATALOG_LOADING;', 'paintState.popup=1;'],
    },
    'LibraryListActivity': {
        'folder': 'library',
        'fields': '''XpUiLibraryState state{};
 struct {XpUiLibraryState snapshot{};XpUiLibraryFeedback next{};XpUiHomeListHits hits{};
 XpUiReaderControl headerControls[4]{};uint8_t headerCount=0;}frame;
 bool resourcesChanged=false,nativeLongPress=false,nativeLongTouch=false;
 int nativeTouchX=0,nativeTouchY=0;uint8_t touchFeedback=0;XpUiHomeListHits publishedHits{};
 XpUiLibraryFacts facts()const{XpUiLibraryFacts f{};f.count=f.books=3;return f;}''',
        'init': 'xp_ui_library_init(&state);state.lists[0].selected=1;',
        'snapshot': 'resourcesChanged=false;frame.snapshot=state;frame.next.list=state.lists[state.active_tab];',
        'stale': ['frame.snapshot.active_tab=1;', 'resourcesChanged=true;'],
    },
    'TextSettingsActivity': {
        'folder': 'settings',
        'fields': '''XpUiTextState state{},rendering{},next{};int renderingScroll=0;
 XpUiReaderHitLayout publishedHits{},nextHits{};bool touchActivated=false;''',
        'init': 'xp_ui_text_init(&state,0,0,0,3,3,0);state.tabs[0].selected=1;',
        'snapshot': 'rendering=next=state;',
        'stale': ['rendering.tab=1;'],
    },
}
code = r'''
#include "RustActivityAdapter.h"
#include <xp_ui_koreader.h>
#include <xp_ui_updates.h>
#include <xp_ui_catalog.h>
#include <xp_ui_home.h>
#include <xp_ui_text_settings.h>
#include <cassert>
#include <cstdio>
'''
for name, spec in families.items():
    path = 'src/activities/' + (spec['folder'] + '/' if spec['folder'] else '') + name
    header = (SOURCE / (path + '.h')).read_text()
    bodies = []
    for ret, member, args in [('void', 'rustPublished', ''),
                              ('bool', 'rustSceneCurrent', ''),
                              ('XpUiInterest', 'rustInterest', 'uint32_t now')]:
        signature = ret + ' ' + member + '(' + args + ')'
        const = ' const' if member != 'rustPublished' else ''
        if signature + const + ' override {' in header:
            body = method(path + '.h', signature + const + ' override')
            body = body.replace(signature + const + ' override', ret + ' ' + name + '::' + member + '(' + args + ')' + const)
        else:
            body = method(path + '.cpp', name + '::' + member + '(')
        bodies.append(body)
    bodies.append(method(path + '.cpp', name + '::updateRust('))
    effect = spec.get('effect', 'if(e.kind){++actions;lastKind=e.kind;lastValue=e.value;}')
    code += f'''
struct {name}:RustActivityAdapter {{
 using RustActivityAdapter::RustActivityAdapter;
 Publication publication;{spec['fields']}
 unsigned actions=0,failures=0;int lastKind=0,lastValue=-1;
 void rustSceneFailed()override{{++failures;}}
 XpUiInterest rustInterest(uint32_t)const override;
 XpUiEffect updateRust(const XpUiInput&)override;
 void applyRustEffect(const XpUiEffect& e)override{{{effect}}}
 Publication* rustPublication()override{{return &publication;}}
 void rustPublished()override;bool rustSceneCurrent()const override;
 void initialize(){{{spec['init']}}}
 void snapshot(){{{spec['snapshot']}}}
 void complete(){{publishRustScene(XpUiScene{{}});}}
 void reject(){{rejectRustScene();}}
 bool rejected()const{{return rustSceneRejected();}}
}};
''' + '\n'.join(bodies) + '\n'
code += r'''
void press(RustActivityAdapter& a,MappedInputManager& i){
 i={};i.pressed=i.held=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
}
void release(RustActivityAdapter& a,MappedInputManager& i){
 i={};i.released=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
}
int main(int argc,char**argv){GfxRenderer r;MappedInputManager i;
'''
for name, spec in families.items():
    for index, stale in enumerate(spec['stale']):
        stale = ';'.join('a.' + part for part in stale.split(';') if part) + ';'
        kind = spec.get('kind', 'XP_UI_ACTIVATE')
        code += f'''
if(argc<2||std::string(argv[1])=="{name}"){{
 // A stale frame closes routes without swallowing a held button's release.
 {{i={{}};{name} a("stale",r,i);a.initialize();a.snapshot();a.complete();
 assert(a.publication.ready);press(a,i);{stale}a.complete();
 assert(!a.publication.ready&&!a.rejected()&&a.failures==0);
 release(a,i);assert(a.actions==1&&a.lastKind=={kind}&&a.lastValue==0);}}
 // Only a current publication may recover a previously rejected scene.
 {{i={{}};{name} a("recovery",r,i);a.initialize();a.snapshot();a.complete();
 press(a,i);a.reject();assert(a.rejected()&&!a.publication.ready&&a.failures==1);
 {stale}a.complete();assert(a.rejected()&&!a.publication.ready&&a.failures==1);
 release(a,i);assert(a.actions==0);
 a.snapshot();a.complete();assert(a.publication.ready&&!a.rejected()&&a.failures==1);
 press(a,i);release(a,i);assert(a.actions==1&&a.lastKind=={kind}&&a.lastValue==0);
 i={{}};a.loop();assert(a.actions==1);}}
 puts("{name} {index}: stale routing, rejection recovery and family action passed");
}}
'''
code += '}\n'
with tempfile.TemporaryDirectory(prefix='stale-publication-') as tmp:
    p = Path(tmp)
    for name in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        (p / name).write_text((SOURCE / 'src/activities' / name).read_text())
    (p / 'check.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', '-I' + str(p), '-I' + str(ROOT / 'test/ui_runtime/stubs'),
                    '-I' + str(ROOT / 'rust/crates/xp-ffi/include'), str(p / 'check.cpp'),
                    str(p / 'RustActivityAdapter.cpp'), str(ROOT / 'test/cmake/xp_ffi_host.cpp'),
                    sys.argv[1], '-o', str(p / 'check')], check=True)
    subprocess.run([str(p / 'check'), *sys.argv[2:]], check=True)
