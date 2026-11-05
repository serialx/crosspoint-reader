"""Inject rejection into production composite renderers and the end-book owner."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


code = r'''
#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"
#include <xp_ui_boot.h>
#include <xp_ui_reader_chrome.h>
#include <xp_ui_dictionary.h>
#include <cassert>
#include <vector>
struct CrossPointSettings {enum {OFF};};
struct {int longPressButtonBehavior=0;} SETTINGS;
constexpr int UI_10_FONT_ID=0, SMALL_FONT_ID=1, UI_12_FONT_ID=2;
constexpr int STR_BACK=0,STR_OPEN=0,STR_DIR_UP=0,STR_DIR_DOWN=0;
const char* tr(int){return "test";}
struct ThemeMetrics {int contentSidePadding=10,topPadding=10,headerHeight=30,verticalSpacing=10;};
struct UITheme {static UITheme getInstance(){return {};} ThemeMetrics getMetrics(){return {};}};
struct {void drawButtonHints(GfxRenderer&,const char*,const char*,const char*,const char*){}} GUI;
struct {const char* get(int){return "message";}} I18N;
struct Scope {void reset(){}};
bool buildOk=true,popupOk=true;
auto dictionaryUiDefinitionLayout(GfxRenderer&){return XpUiDefinitionLayout{};}
bool dictionaryUiPopup(GfxRenderer&,const char*,XpUiCommand*,int,XpUiScene&){return popupOk;}
void sampleReaderChromeTheme(GfxRenderer&,XpUiReaderTheme&){}
// Only view return values are injected; publication and rejection code is production.
int buildBoot(const XpUiBootLayout*,const XpUiBootMetrics*,const XpUiBuffer*,XpUiScene*){return buildOk;}
int buildEnd(const XpUiReaderListState*,int,const XpUiReaderTheme*,const XpUiReaderRows*,const XpUiBuffer*,XpUiReaderListState*,XpUiScene*,XpUiReaderHitLayout*){return buildOk?XP_UI_READER_VIEW_OK:0;}
int buildDefinition(const XpUiDefinitionState*,const XpUiDefinitionLayout*,const XpUiBuffer*,XpUiScene*){return buildOk;}
int buildLookup(const XpUiLookupState*,XpUiRect,const XpUiBuffer*,XpUiScene*){return buildOk;}
class BootUiActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 struct ViewData {const char* subtitle=nullptr;const char* error=nullptr;const std::vector<std::string>* description=nullptr;const std::vector<std::string>* panic=nullptr;};
 const ViewData* activeView=nullptr;
 bool deferPublication=false,viewsValid=true;
 const char* resourceText(uint32_t)const{return "text";}
 bool paintBootViewInto(XpUiBootView,XpUiRefresh,const ViewData&,uint16_t,const XpUiBuffer&);
 bool failed(){return rustSceneRejected();}
'''
code += method('src/activities/boot_sleep/BootUiActivity.h', '  class ViewPublication') + ';\n};\n'
code += method('src/activities/boot_sleep/BootUiActivity.cpp', 'bool BootUiActivity::paintBootViewInto(').replace('xp_ui_boot_view(', 'buildBoot(')
code += r'''
class BmpViewerActivity:public BootUiActivity {
 public:
 using BootUiActivity::BootUiActivity;
 int paints=0;
 void paintImageView(){++paints;XpUiBuffer b{};paintBootViewInto(XP_UI_BOOT_IMAGE_HINTS,XP_UI_NONE,{},0,b);}
'''
code += method('src/activities/util/BmpViewerActivity.h', '  void render(') + '\n};\n'
code += r'''
class EndOfBookOptions {
 public:
 enum class Action {None,Redraw,OpenBook,GoHome,LastPage};
 GfxRenderer& renderer;
 EndOfBookOptions(GfxRenderer& r):renderer(r){xp_ui_reader_list_init(&state,0);}
 XpUiReaderListState state{},next{};
 XpUiReaderTheme theme{};
 XpUiCommand commands[16]{};
 XpUiReaderHitLayout published{};
 portMUX_TYPE mutex;
 bool ready=false,sceneFailed=false,suppressedContact=false,lastContact=false;
 uint32_t suppressed=0,lastHeld=0;
 const MappedInputManager* renderingInput=nullptr;
 uint16_t rowCount=3;
 XpUiReaderRows resources(){return {};}
 const char* text(uint32_t)const{return "text";}
 std::string fullPath(size_t i)const{return "book"+std::to_string(i);}
 Action handleMenuInput(const MappedInputManager&,std::string*);
 void render(GfxRenderer&,const MappedInputManager&);
};
'''
for sig in ['EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(', 'void EndOfBookOptions::render(']:
    code += method('src/activities/reader/EndOfBookOptions.cpp', sig).replace('xp_ui_reader_list_view(', 'buildEnd(')
code += r'''
class DictionaryDefinitionActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 struct {int currentPage=0,totalPages=1;} content;
 struct Lookup {int depth=0;} lookupData,*lookup=nullptr;
 bool messageVisible=true,initialLayoutFailed=false;
 int message=0;
 XpUiDefinitionState rendered{};
 XpUiCommand commands[4]{};
 Scope framePrewarm;
 unsigned effects=0;
 int lastEffect=0;
 void applyNavigation(const XpUiEffect& e,int,int){++effects;lastEffect=e.kind;}
 void cancelRejectedInput(const XpUiInput&) override;
 void render(RenderLock&&) override;
 bool failed(){return rustSceneRejected();}
};
class DictionaryWordSelectActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 XpUiLookupState state{};
 XpUiCommand commands[3]{};
 Scope framePrewarm;
 int popupMsg=0;
 void render(RenderLock&&) override;
 bool failed(){return rustSceneRejected();}
};
'''
code += method('src/activities/reader/DictionaryDefinitionActivity.cpp', 'void DictionaryDefinitionActivity::render(').replace('xp_ui_definition_view(', 'buildDefinition(')
code += method('src/activities/reader/DictionaryDefinitionActivity.cpp', 'void DictionaryDefinitionActivity::cancelRejectedInput(')
code += method('src/activities/reader/DictionaryWordSelectActivity.cpp', 'void DictionaryWordSelectActivity::render(').replace('xp_ui_lookup_view(', 'buildLookup(')
code += r'''
XpUiInput dictionaryUiInput(const MappedInputManager&){return {};}
int buildSelection(const XpUiSelectionState*,const XpUiSelectionWord*,uint32_t,const XpUiSelectionLayout*,const XpUiSelectionPainted*,const XpUiBuffer*,XpUiSelectionPainted*,XpUiScene*){return buildOk;}
class ClipSelectionActivity:public RustActivityAdapter {
 public:
 using RustActivityAdapter::RustActivityAdapter;
 XpUiSelectionState state{},rendered{};
 XpUiSelectionPainted painted{},nextPainted{};
 XpUiSelectionLayout layout{};
 XpUiCommand commands[20]{};
 Scope framePrewarm;
 int wordCount=1;
 unsigned actions=0,cancellations=0;
 struct Popup {
  bool active=false;
  bool isActive()const{return active;}
  bool processRender(GfxRenderer&,MappedInputManager&){return false;}
  template<class F> bool handleInput(MappedInputManager&,F){return false;}
  template<class F> bool handleButtons(bool,bool,bool,bool back,F f){if(back)active=false;f();return back;}
 } actionPopup;
 void cancel(){++cancellations;}
 void loopButtons(){++actions;}
 void drainRejectedTouch();
 const XpUiSelectionWord* wireWords(){return nullptr;}
 XpUiSelectionLayout sampleLayout(){return {};}
 void applySelection(const XpUiSelectionUpdate&){++actions;}
 void loop()override;
 void render(RenderLock&&)override;
 void rustSceneFailed()override;
 void cancelRejectedInput(const XpUiInput&)override;
};
'''
for signature in ['void ClipSelectionActivity::drainRejectedTouch(', 'void ClipSelectionActivity::loop(', 'void ClipSelectionActivity::render(']:
    code += method('src/activities/reader/ClipSelectionActivity.cpp', signature).replace('xp_ui_selection_view(', 'buildSelection(')
for name in ['rustSceneFailed', 'cancelRejectedInput']:
    code += method('src/activities/reader/ClipSelectionActivity.h', '  void '+name+'(').replace('  void '+name+'(', 'void ClipSelectionActivity::'+name+'(').replace(' override', '')
code += r'''
int main(){
 for(bool first:{false,true})for(bool buildFailure:{false,true}){
  GfxRenderer r;MappedInputManager input;std::string path="unchanged";
  EndOfBookOptions end(r);
  input.released=1u<<unsigned(MappedInputManager::Button::Confirm);
  assert(end.handleMenuInput(input,&path)==EndOfBookOptions::Action::OpenBook);
  input={};if(first)end.render(r,input);
  buildOk=!buildFailure;r.paintOk=buildFailure;end.render(r,input);assert(end.sceneFailed&&!end.ready);
  for(auto b:{MappedInputManager::Button::Confirm,MappedInputManager::Button::NavNext}){
   path="unchanged";input.released=1u<<unsigned(b);
   assert(end.handleMenuInput(input,&path)==EndOfBookOptions::Action::Redraw&&path=="unchanged");
  }
  input={};input.touchFlags=XP_UI_TOUCH_RELEASED;
  assert(end.handleMenuInput(input,&path)==EndOfBookOptions::Action::Redraw);
  input={};input.released=1u<<unsigned(MappedInputManager::Button::Back);
  assert(end.handleMenuInput(input,&path)==EndOfBookOptions::Action::LastPage);
  input.heldTime=1000;assert(end.handleMenuInput(input,&path)==EndOfBookOptions::Action::None);
  input={};buildOk=r.paintOk=true;end.render(r,input);assert(!end.sceneFailed&&end.ready);
  input.released=1u<<unsigned(MappedInputManager::Button::Confirm);
  assert(end.handleMenuInput(input,&path)==EndOfBookOptions::Action::OpenBook);
  input={};ClipSelectionActivity clip("clip",r,input);
  auto paintClip=[&]{RenderLock lock;clip.render(std::move(lock));};
  if(first)paintClip();
  clip.state.pending_count=3;clip.state.dragging=1;
  buildOk=!buildFailure;r.paintOk=buildFailure;paintClip();
  assert(!clip.state.pending_count&&!clip.state.dragging);
  input.released=1u<<unsigned(MappedInputManager::Button::Confirm);clip.loop();assert(!clip.actions);
  input={};input.touchFlags=XP_UI_TOUCH_RELEASED;clip.loop();assert(!clip.actions);
  input={};input.released=1u<<unsigned(MappedInputManager::Button::Back);clip.loop();assert(clip.cancellations==1);
  clip.actionPopup.active=true;clip.loop();assert(!clip.actionPopup.active&&clip.cancellations==1);
  input={};buildOk=r.paintOk=true;paintClip();clip.loop();assert(clip.actions==1);
  input={};BootUiActivity boot("boot",r,input);
  XpUiBuffer buffer{};
  auto paint=[&]{return boot.paintBootViewInto(XP_UI_BOOT_IMAGE_HINTS,XP_UI_NONE,{},0,buffer);};
  if(first)assert(paint());
  {
   BootUiActivity::ViewPublication frame(boot);
   buildOk=!buildFailure;r.paintOk=buildFailure;assert(!paint());
   buildOk=r.paintOk=true;assert(paint());assert(boot.failed());
  }
  assert(boot.failed());
  {BootUiActivity::ViewPublication frame(boot);assert(paint());assert(paint());}
  assert(!boot.failed());
  BmpViewerActivity image("image",r,input);
  const auto retryImage=[&]{RenderLock lock;image.render(std::move(lock));};
  retryImage();assert(image.paints==0); // Entry owns the initial image paint.
  image.paintImageView();retryImage();assert(image.paints==1);
  buildOk=!buildFailure;r.paintOk=buildFailure;image.paintImageView();
  assert(image.failed());retryImage();assert(image.paints==3&&image.failed());
  buildOk=r.paintOk=true;retryImage();assert(image.paints==4&&!image.failed());
  retryImage();assert(image.paints==4); // Dialog cancellation already repainted in its callback.
  DictionaryDefinitionActivity definition("definition",r,input);
  DictionaryWordSelectActivity lookup("lookup",r,input);lookup.state.popup=1;
  const auto render=[](auto& a){RenderLock lock;a.render(std::move(lock));};
  if(first){render(definition);render(lookup);}
  buildOk=!buildFailure;r.paintOk=buildFailure;render(definition);render(lookup);
  assert(definition.failed()&&lookup.failed());
  input.released=1u<<unsigned(MappedInputManager::Button::Confirm);definition.loop();assert(!definition.effects);
  input.released=1u<<unsigned(MappedInputManager::Button::Back);definition.loop();
  assert(definition.lastEffect==XP_UI_DEFINITION_FINISH);
  definition.lookup=&definition.lookupData;definition.lookup->depth=1;definition.loop();
  assert(definition.lastEffect==XP_UI_DEFINITION_BACK);
  input.heldTime=1000;definition.loop();assert(definition.lastEffect==XP_UI_DEFINITION_CANCEL);
  input={};buildOk=r.paintOk=true;popupOk=false;render(definition);render(lookup);
  assert(definition.failed()&&lookup.failed()); // Main view cannot publish past popup rejection.
  popupOk=true;render(definition);render(lookup);assert(!definition.failed()&&!lookup.failed());
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    for name in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        (out / name).write_bytes((ROOT / 'src/activities' / name).read_bytes())
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('class GfxRenderer {', 'struct EpdFontFamily {enum Style {REGULAR};};\nclass GfxRenderer {')
    stub = stub.replace('  bool paintOk = true;', '''  int getScreenWidth()const{return 600;}
  int getScreenHeight()const{return 800;}
  int getTextWidth(int,const char*,EpdFontFamily::Style)const{return 40;}
  void drawUiText(const XpUiCommand&,XpUiRect,const char*){}
  bool paintOk = true;''')
    stub = stub.replace('  SwipeDir swipe', '''  bool hasTouch()const{return touchFlags;}
  bool isScreenTouchHeld(int& x,int& y)const{x=y=10;return touchFlags&XP_UI_TOUCH_HELD;}
  bool wasScreenTouchPressed(int& x,int& y)const{x=y=10;return touchFlags&XP_UI_TOUCH_PRESSED;}
  bool wasScreenTapped(int& x,int& y)const{x=y=10;return touchFlags&XP_UI_TOUCH_RELEASED;}
  SwipeDir swipe''')
    stub = stub.replace('  SwipeDir swipe', '''  struct Labels {const char* btn1;const char* btn2;const char* btn3;const char* btn4;};
  Labels mapLabels(const char* a,const char* b,const char* c,const char* d)const{return {a,b,c,d};}
  SwipeDir swipe''')
    (out / 'Activity.h').write_text(stub)
    (out / 'composite.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'),
                    str(out/'composite.cpp'), str(out/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1], '-o', str(out/'composite')], check=True)
    subprocess.run([str(out/'composite')], check=True, timeout=30)
print('Composite Boot/image, dictionary and end-book rejection paths pass')
