"""An obsolete build must not publish routes for a different phase/tab/popup."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('XP_REJECTION_SOURCE_ROOT', ROOT))

def method(path, signature):
    s = (SOURCE/path).read_text()
    start=s.index(signature);start=s.rfind('\n',0,start)+1
    end=s.index('{',start)+1;depth=1
    while depth:
        depth+=(s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]

families = {
 'SettingsUiActivity': ('settings', '''XpUiListState state{},next{};int renderingScroll=0;XpUiSettingsHits publishedHits{},nextHits{};bool sceneFailed=false;''',
                      ['state.navigation_revision=1;next.navigation_revision=0;']),
 'KOReaderUiActivity': ('reader', '''XpKoState model{},painting{};uint8_t nextHits=0;bool sceneFailed=false;''',
                      ['model.phase=1;painting.phase=0;']),
 'UpdateUiActivity': ('settings', '''XpUiUpdateState model{},painting{};XpUiUpdateFeedback feedback{},next{};bool routesReady=false;''',
                    ['model.phase=1;painting.phase=0;', 'model.popup_active=1;painting.popup_active=0;']),
 'CatalogUiActivity': ('', '''XpUiCatalogState ui{},paintState{};XpUiListState next{};XpUiCatalogHits publishedHits{},nextHits{};int consumedScroll=0;bool sceneFailed=false;''',
                     ['ui.mode=1;paintState.mode=0;', 'ui.popup=1;paintState.popup=0;']),
 'LibraryListActivity': ('library', '''XpUiLibraryState state{};struct {XpUiLibraryState snapshot{};XpUiLibraryFeedback next{};XpUiHomeListHits hits{};XpUiReaderControl headerControls[4]{};uint8_t headerCount=0;}frame;bool resourcesChanged=false;XpUiHomeListHits publishedHits{};''',
                         ['state.active_tab=1;frame.snapshot.active_tab=0;', 'resourcesChanged=true;']),
 'TextSettingsActivity': ('settings', '''XpUiTextState state{},rendering{},next{};int renderingScroll=0;XpUiSettingsHits publishedHits{},nextHits{};''',
                          ['state.tab=1;rendering.tab=0;']),
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
for name,(folder,fields,cases) in families.items():
    path='src/activities/'+(folder+'/' if folder else '')+name
    header=(SOURCE/(path+'.h')).read_text()
    cpp=(SOURCE/(path+'.cpp')).read_text()
    if 'void rustPublished() override {' in header:
        publish=method(path+'.h','void rustPublished() override').replace('void rustPublished() override','void '+name+'::rustPublished()')
    else:
        publish=method(path+'.cpp',name+'::rustPublished(')
    current=''
    if name+'::rustSceneCurrent(' in cpp:
        current=method(path+'.cpp',name+'::rustSceneCurrent(')
    elif 'bool rustSceneCurrent() const override {' in header:
        current=method(path+'.h','bool rustSceneCurrent() const override').replace('bool rustSceneCurrent() const override','bool '+name+'::rustSceneCurrent() const')
    decl='bool rustSceneCurrent() const override;' if current else ''
    code+=f'''\nstruct {name}:RustActivityAdapter {{
 using RustActivityAdapter::RustActivityAdapter;
 Publication publication;{fields}
 unsigned releases=0,failures=0;
 void rustSceneFailed()override{{++failures;}}
 XpUiInterest rustInterest(uint32_t)const override{{XpUiInterest v{{}};v.pressed=v.released=v.held=XP_UI_CONFIRM;return v;}}
 XpUiEffect updateRust(const XpUiInput& v)override{{releases+=(v.released&XP_UI_CONFIRM)!=0;return {{}};}}
 Publication* rustPublication()override{{return &publication;}}
 void rustPublished()override;{decl}
 void complete(){{publishRustScene(XpUiScene{{}});}}
}};\n'''+publish+'\n'+current+'\n'
code+='int main(int argc,char**argv){GfxRenderer r;MappedInputManager i;\n'
for name,(_,_,cases) in families.items():
    for index,setup in enumerate(cases):
        setup=";".join("a."+part for part in setup.split(";") if part)+";"
        assertion="!a.publication.ready || !a.routesReady" if name=="UpdateUiActivity" else "!a.publication.ready"
        code+=f'''if(argc<2||std::string(argv[1])=="{name}"){{ {name} a("stale",r,i);{setup}
 i={{}};i.pressed=i.held=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
 a.complete();assert({assertion});assert(a.failures==0);
 i.pressed=i.held=0;i.released=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();assert(a.releases==1);puts("{name} {index} stale routes blocked");}}\n'''
code+='}\n'
with tempfile.TemporaryDirectory(prefix='stale-publication-') as tmp:
    p=Path(tmp)
    for n in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:
        (p/n).write_text((SOURCE/'src/activities'/n).read_text())
    (p/'check.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-I'+str(p),'-I'+str(ROOT/'test/ui_runtime/stubs'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(p/'check.cpp'),str(p/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check'),*sys.argv[2:]],check=True)
