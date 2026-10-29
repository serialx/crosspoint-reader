"""Exercise production family callbacks through the production rejection gate."""
from pathlib import Path
import os
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('XP_REJECTION_SOURCE_ROOT', ROOT))
ARCHIVE = Path(sys.argv[1])

def method(path, signature):
    file = SOURCE / path
    source = (file if file.exists() else ROOT / path).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[start:end]
    if '::updateRust(' in signature:
        body = body.replace('{', '{\n ++updates;', 1)
    return body

code = r'''
#include "RustActivityAdapter.h"
#include <xp_ui_utility.h>
#include <xp_ui_home.h>
#include <xp_ui_boot.h>
#include <xp_ui_text_settings.h>
#include <cassert>
#include <vector>
struct Probe : RustActivityAdapter {
  Publication publication;
  bool buildOk = true;
  unsigned updates = 0, effects = 0, writes = 0;
  XpUiEffect lastEffect{};
  Probe(GfxRenderer& r, MappedInputManager& i):RustActivityAdapter("fault",r,i){}
  Publication* rustPublication() override {return &publication;}
  bool buildRustScene(XpUiScene& scene) override {scene.refresh=XP_UI_NONE;return buildOk;}
  void applyRustEffect(const XpUiEffect& e) override {lastEffect=e;effects += e.kind != 0;}
};
class UtilityActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiUtilityState state{};
  XpUiUtilityEffect pendingEffect{};
  XpUiUtilityFeedback feedback{};
  XpUiEffect updateRust(const XpUiInput&) override;
  void rustSceneFailed() override;
  void cancelRejectedInput(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t now) const override {return xp_ui_utility_interest(&state,now);}
  void applyRustEffect(const XpUiEffect&) override {applyUtilityEffect(pendingEffect);}
  void applyUtilityEffect(const XpUiUtilityEffect& e) {effects += e.navigation.kind != 0;writes += e.writes;}
};
struct CoverGridHomeUi {
 static constexpr int THUMB_HEIGHT=100;
 bool prepare(const XpUiHomeGridGeometry&){return false;}
 void refreshCoverPaths(){}
};
class HomeActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiHomeState state{};
  XpUiHomeTouch publishedTouch{};
  XpUiHomeGridGeometry publishedGrid{};
  CoverGridHomeUi* coverGridUi=nullptr;
  struct {XpUiHomeGridGeometry grid{};XpUiHomeLayout layout{};} frame;
  void loadRecentCovers(int){}
  void render(RenderLock&&) override;
  bool touchActivated=false;
  XpUiEffect updateRust(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t now) const override {return xp_ui_home_interest(&state,now);}
  void cancelRejectedInput(const XpUiInput&) override {}
  void rustSceneFailed() override {touchActivated=false;}
};
class BmpViewerActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiImageState state{0,3,1,{}};
  XpUiEffect updateRust(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t) const override;
};
class EpubReaderFootnoteSelectActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiReaderFootnoteState state{};
  uint16_t footnoteLinkCount=3;
  XpUiEffect updateRust(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t now) const override {return xp_ui_reader_footnote_interest(&state,now);}
};
class ReaderChromeListActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiReaderListState state{};
  XpUiReaderHitLayout publishedHits{};
  bool renderList(bool refresh=true);
  void render(RenderLock&&) override {renderList();}
  uint8_t kind=XP_UI_READER_BOOKMARKS;
  bool nativeLongPress=false,nativeLongTouch=false,touchActivated=false;
  int nativeTouchX=0,nativeTouchY=0;
  int listCount() const{return 3;}
  bool hasModel() const{return true;}
  XpUiEffect updateRust(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t) const override;
  void rustSceneFailed() override;
};

class FileBrowserActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiFileState state{};
  XpUiHomeListHits publishedHits{};
  bool nativeLongPress=false,nativeLongTouch=false,root=true;
  uint8_t touchFeedback=0;
  int nativeTouchX=0,nativeTouchY=0;
  std::vector<std::string> files{"one.epub","two.epub","dir/"};
  int listCount() const{return 3;}
  XpUiFileFacts facts(int) const{return {3,uint8_t(root),0,uint8_t(nativeLongPress),uint8_t(nativeLongTouch),0,0};}
  XpUiEffect updateRust(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t) const override;
  void rustSceneFailed() override;
  void cancelRejectedInput(const XpUiInput&) override;
};
class LibraryListActivity:public Probe {
 public:
  using Probe::Probe;
  XpUiLibraryState state{};
  XpUiHomeListHits publishedHits{};
  bool nativeLongPress=false,nativeLongTouch=false;
  uint8_t touchFeedback=0;
  int nativeTouchX=0,nativeTouchY=0;
  XpUiLibraryFacts facts() const{return {3,3,uint8_t(nativeLongPress),uint8_t(nativeLongTouch),0,0};}
  XpUiEffect updateRust(const XpUiInput&) override;
  XpUiInterest rustInterest(uint32_t) const override;
  void rustSceneFailed() override;
  void cancelRejectedInput(const XpUiInput&) override;
};
class TextSettingsActivity:public Probe {
 public:
 using Probe::Probe;
 XpUiTextState state{};
 XpUiReaderHitLayout publishedHits{};
 bool touchActivated=false;
 XpUiEffect updateRust(const XpUiInput&) override;
 XpUiInterest rustInterest(uint32_t now) const override{return xp_ui_text_interest(&state,now);}
 void rustSceneFailed() override{touchActivated=false;}
};
class XtcReaderChapterSelectionActivity:public Probe {
 public:
 using Probe::Probe;
 struct Book {std::vector<int> getChapters()const{return {0,1,2};}} book,*xtc=&book;
 XpUiListState state{};
 XpUiEffect updateRust(const XpUiInput&) override;
 XpUiInterest rustInterest(uint32_t now)const override{return xp_ui_list_interest(&state,now);}
 void rustSceneFailed() override{state.last_repeat=0;}
};

'''
for name,path,sigs in [
    ('TextSettingsActivity','settings',['XpUiEffect']),
    ('XtcReaderChapterSelectionActivity','reader',['XpUiEffect']),
    ('FileBrowserActivity','home',['XpUiEffect','XpUiInterest','void rustSceneFailed','void cancelRejectedInput']),
    ('LibraryListActivity','library',['XpUiEffect','XpUiInterest','void rustSceneFailed','void cancelRejectedInput']),
    ('UtilityActivity','util',['XpUiEffect','void rustSceneFailed','void cancelRejectedInput']),
    ('HomeActivity','home',['XpUiEffect','void render']),
    ('BmpViewerActivity','util',['XpUiEffect','XpUiInterest']),
    ('EpubReaderFootnoteSelectActivity','reader',['XpUiEffect']),
    ('ReaderChromeListActivity','reader',['XpUiEffect','XpUiInterest','void rustSceneFailed','bool renderList'])]:
    for sig in sigs:
        parts=sig.split()
        typename=parts[0]
        function=parts[1] if len(parts)>1 else ('updateRust' if typename=='XpUiEffect' else 'rustInterest')
        code += method(f'src/activities/{path}/{name}.cpp',f'{typename} {name}::{function}(')+'\n'
code += r'''
template<class Family> void exercise(int kind=-1) {
  for(bool first:{false,true}) for(bool buildFailure:{false,true}) {
    GfxRenderer renderer;
    MappedInputManager input;
    Family family(renderer,input);
    if constexpr(std::is_same_v<Family,UtilityActivity>) {family.state.kind=kind;xp_ui_utility_init(&family.state);}
    if constexpr(std::is_same_v<Family,HomeActivity>) xp_ui_home_init(&family.state,3,0,0,0,1,0);
    if constexpr(std::is_same_v<Family,ReaderChromeListActivity>) {family.kind=kind;xp_ui_reader_list_init(&family.state,0);}
    if constexpr(std::is_same_v<Family,FileBrowserActivity>) xp_ui_file_init(&family.state,0);
    if constexpr(std::is_same_v<Family,LibraryListActivity>) xp_ui_library_init(&family.state);
    const auto paint=[&] {RenderLock lock;family.render(std::move(lock));};
    input.released=1u<<unsigned(MappedInputManager::Button::Confirm);
    family.loop();
    assert(family.updates==1); // Pre-first-paint remains live.
    input={};
    if(first)paint();
    family.buildOk=!buildFailure;renderer.paintOk=buildFailure;
    paint();
    const auto before=family.updates, effects=family.effects, writes=family.writes;
    for(auto button:{MappedInputManager::Button::Confirm,MappedInputManager::Button::NavNext,
                     MappedInputManager::Button::Power}) {
      input={};input.pressed=input.held=1u<<unsigned(button);input.heldTime=1001;
      family.loop();
      input.pressed=input.held=0;input.released=1u<<unsigned(button);family.loop();
    }
    input={};input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;family.loop();
    input.touchFlags=XP_UI_TOUCH_RELEASED;family.loop();
    assert(family.updates==before && family.effects==effects && family.writes==writes);
    input={};input.released=1u<<unsigned(MappedInputManager::Button::Back);family.loop();
    if constexpr(std::is_same_v<Family,HomeActivity>) assert(family.effects==effects);
    else assert(family.effects==effects+1 && family.writes==writes);
    input={};family.buildOk=renderer.paintOk=true;paint();
    input.released=1u<<unsigned(MappedInputManager::Button::Confirm);family.loop();
    assert(family.updates==before+1);
  }
}
void backPolicies() {
  GfxRenderer renderer;
  MappedInputManager input;
  XpUiInput back{};
  back.released=XP_UI_BACK;
  for (bool root:{false,true}) for (uint8_t picker:{0,1}) for(uint32_t held:{0u,1001u}) {
    FileBrowserActivity family(renderer,input);
    xp_ui_file_init(&family.state,picker);family.root=root;
    auto original=family.state;
    auto facts=family.facts(0);
    back.held_ms=held;
    const auto expected=xp_ui_file_update(&original,&back,&facts).effect;
    family.cancelRejectedInput(back);
    assert(family.lastEffect.kind==expected.kind && family.lastEffect.value==expected.value);
  }
  for (uint8_t query:{0,1}) for(uint8_t tab:{0,1,2}) {
    LibraryListActivity family(renderer,input);
    xp_ui_library_init(&family.state);family.state.queried=query;family.state.active_tab=tab;
    auto original=family.state;
    auto facts=family.facts();back.held_ms=0;
    const auto expected=xp_ui_library_update(&original,&back,&facts).effect;
    family.cancelRejectedInput(back);
    assert(family.lastEffect.kind==expected.kind && family.lastEffect.value==expected.value);
  }
}
int main() {
  backPolicies();
  for(int kind:{XP_UI_UTILITY_CONFIRMATION,XP_UI_UTILITY_INTERVAL,XP_UI_UTILITY_DETAILS,XP_UI_UTILITY_FRONTLIGHT}) exercise<UtilityActivity>(kind);
  for(int kind:{XP_UI_READER_MENU,XP_UI_READER_CHAPTERS,XP_UI_READER_BOOKMARKS,XP_UI_READER_CLIPPINGS}) exercise<ReaderChromeListActivity>(kind);
  exercise<FileBrowserActivity>();exercise<LibraryListActivity>();
  exercise<TextSettingsActivity>();exercise<XtcReaderChapterSelectionActivity>();
  exercise<HomeActivity>();exercise<BmpViewerActivity>();exercise<EpubReaderFootnoteSelectActivity>();
}
'''
with tempfile.TemporaryDirectory() as directory:
    out=Path(directory)
    (out/'families.cpp').write_text(code)
    for name in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        (out/name).write_bytes((SOURCE/'src/activities'/name).read_bytes())
    subprocess.run(['c++','-std=c++20','-pthread','-UNDEBUG',
                    '-I'+str(out),'-I'+str(ROOT/'test/ui_runtime/stubs'),
                    '-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(out/'families.cpp'),
                    str(out/'RustActivityAdapter.cpp'),str(ROOT/'test/cmake/xp_ffi_host.cpp'),
                    str(ARCHIVE),'-o',str(out/'families')],check=True)
    subprocess.run([str(out/'families')],check=True,timeout=30)
print('Family build/paint rejection: before/after publication, activation, navigation, touch, Back and recovery pass')
