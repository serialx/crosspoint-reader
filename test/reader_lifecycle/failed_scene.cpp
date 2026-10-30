#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"
#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <string>

// Only hardware drawing and resource metrics are stubbed; publication, routing,
// command growth, parent policy and the failed-scene gate are production code.
namespace freeink::ui {
struct Rect {int16_t x=0,y=0,width=528,height=792;};
struct BitmapRef {};
struct ListItem {bool toggle=false,toggleChecked=false;};
struct ListNav {
  int selected=0,top=0,visibleRows=1,drawnRows=0,drawnCount=0;
  void reset(){*this={};}
  int pageRows()const{return drawnRows>0?drawnRows:visibleRows;}
};
enum class BitmapMode {Center};
struct TextStyle {int font=0;uint8_t bold=0,maxLines=1;};
struct GfxRendererTarget {
  struct Device {Rect safeRect() const {return {};}};
  Device deviceContext() const {return {};}
  void setFont(int,int) {}
  void bitmap(Rect,BitmapRef,BitmapMode) {}
};
inline Rect measureWrappedText(GfxRendererTarget&,const char*,TextStyle,int16_t) {return {0,0,20,20};}
inline BitmapRef bitmapFromIcon(int) {return {};}
}
namespace fui=freeink::ui;
struct EpdFontFamily {enum Style {Regular};};
struct UITheme {
  static UITheme& getInstance(){static UITheme t;return t;}
  struct Metrics {int listRowHeight=48;};
  Metrics getMetrics() const {return {};}
};
constexpr int icon_reader_contents_24=0,icon_reader_text_24=1,icon_reader_more_24=2,
  icon_reader_back_24=3,icon_reader_next_24=4;
static bool allocationFails=false;
static int failure=0;
static unsigned failedAllocations=0;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t n) {
  if(allocationFails){++failedAllocations;return {};}
  return std::make_unique<T>(n);
}
void sampleReaderChromeTheme(const GfxRenderer&,XpUiReaderTheme& t) {
  t={};t.screen=t.safe={0,0,528,792};t.body_font=1;t.small_font=0;t.title_font=2;
  t.row_min=48;t.row_padding=8;t.scroll_width=3;t.space_sm=4;t.space_md=8;t.space_lg=16;
  t.control_radius=t.sheet_radius=12;
}
static auto injectedView(const XpUiListState* state,const XpUiReaderToolbarModel* model,
 const XpUiReaderTheme* theme,const XpUiReaderRows* rows,const XpUiBuffer* buffer,
 XpUiListState* next,XpUiScene* scene,XpUiReaderHitLayout* hits) {
  if(failure==1)return uint8_t(XP_UI_READER_VIEW_INVALID);
  if(failure==3){allocationFails=true;return uint8_t(XP_UI_READER_VIEW_CAPACITY);}
  return xp_ui_reader_toolbar_view(state,model,theme,rows,buffer,next,scene,hits);
}
// COMMAND_CLASS
// TOOLBAR_CLASS
ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& r):renderer(r){}
// TOOLBAR_METHODS

class EpubReaderActivity:public RustActivityAdapter {
 public:
  enum class Overlay {None,Toolbar,Contents,Text,More};
  Overlay overlay=Overlay::Toolbar;
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  ReaderToolbarUi::Routed routed{};
  unsigned actions=0,notifications=0,cancelled=0;
  uint32_t fields[8]{XP_UI_READER_OVERLAY_MODE_TOOLBAR};
  EpubReaderActivity(GfxRenderer& r,MappedInputManager& i):RustActivityAdapter("reader",r,i),toolbarUi(std::make_unique<ReaderToolbarUi>(r)) {
    toolbarUi->begin();
  }
  void rustSceneFailed() override {++notifications;}
  // CANCEL_DECL
  void closeOverlayToPage(){++cancelled;++actions;overlay=Overlay::None;}
  void loop() override;
  void build() {
    auto& o=*this;
    ReaderToolbarUi::Model model;
    model.chapterTitle="Chapter";model.pageInfo="1/10";
    // PUBLICATION
  }
  void handleOverlayInput() {
    const XpUiReaderOverlayServices services{
      this,
      [](void* p,uint8_t f){return static_cast<EpubReaderActivity*>(p)->fields[f];},
      [](void* p,uint8_t f,uint32_t v){static_cast<EpubReaderActivity*>(p)->fields[f]=v;},
      [](void* p,uint8_t q)->uint32_t {
        auto& o=*static_cast<EpubReaderActivity*>(p);
        switch(static_cast<XpUiReaderOverlayQuery>(q)){
          case XP_UI_READER_OVERLAY_QUERY_HAS_TOOLBAR:return 1;
          case XP_UI_READER_OVERLAY_QUERY_POPUP_ACTIVE:return 0;
          case XP_UI_READER_OVERLAY_QUERY_ROUTED:return o.routed.routed;
          case XP_UI_READER_OVERLAY_QUERY_VALUE:return o.routed.value;
          case XP_UI_READER_OVERLAY_QUERY_RELEASED:return (o.mappedInput.wasReleased(MappedInputManager::Button::Confirm)?XP_UI_CONFIRM:0) | (o.mappedInput.wasReleased(MappedInputManager::Button::Back)?XP_UI_BACK:0);
          default:assert(false);return 0;
        }
      },
      [](void* p,uint8_t c,int32_t,int32_t)->uint32_t {
        auto& o=*static_cast<EpubReaderActivity*>(p);
        switch(static_cast<XpUiReaderOverlayCommand>(c)){
          case XP_UI_READER_OVERLAY_COMMAND_ROUTE:o.routed=o.toolbarUi->route(o.mappedInput);return static_cast<uint32_t>(o.routed.event);
          case XP_UI_READER_OVERLAY_COMMAND_SAMPLE_BUTTONS:return 0;
          case XP_UI_READER_OVERLAY_COMMAND_OPEN:++o.actions;return 0;
          case XP_UI_READER_OVERLAY_COMMAND_CLOSE:o.closeOverlayToPage();return 0;
          default:assert(false);return 0;
        }
      }};
    xp_ui_reader_overlay_run(&services,XP_UI_READER_OVERLAY_OPERATION_INPUT);
  }
};
// LOOP_METHOD
// CANCEL_METHOD

int main(int argc,char** argv){
  assert(argc==4);
  const std::string stage=argv[1],kind=argv[2],check=argv[3];
  GfxRenderer r;MappedInputManager input;EpubReaderActivity a(r,input);
  const auto confirm=1u<<static_cast<unsigned>(MappedInputManager::Button::Confirm);
  if(stage!="first")a.build();
  // Track a real held gesture on the current scene before the failed rebuild.
  if(check=="held-button"){input.held=confirm;a.loop();}
  if(check=="held-touch"){input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();}
  input={};
  if(stage=="setting")a.toolbarUi->resetNavigation(0,0);
  if(kind=="reserve"){a.toolbarUi->commands.reset();allocationFails=true;}
  if(kind=="grow")failure=3;
  if(kind=="builder")failure=1;
  if(kind=="paint")r.paintOk=false;
  a.build();
  if(kind=="reserve"||kind=="grow")assert(failedAllocations==1);
  const auto previous=a.actions;
  if(check=="button"){input.released=confirm;a.loop();assert(a.actions==previous);}
  if(check=="touch"){input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();assert(a.actions==previous);}
  if(check=="notification")assert(a.notifications==1);
  if(check=="retry"){a.loop();assert(a.requested>0);}
  if(check=="busy"){
    RenderLock busy;
    auto inputTask=std::async(std::launch::async,[&]{input.released=confirm;a.loop();});
    assert(inputTask.wait_for(std::chrono::seconds(1))==std::future_status::ready);inputTask.get();assert(a.actions==previous);
  }
  if(check=="back"){
    input.released=1u<<static_cast<unsigned>(MappedInputManager::Button::Back);a.loop();assert(a.cancelled==1);return 0;
  }
  allocationFails=false;failure=0;r.paintOk=true;
  a.build();
  if(check=="held-button"){input.held=confirm;a.loop();input.held=0;input.released=confirm;a.loop();assert(a.actions==previous);}
  if(check=="held-touch"){input.touchFlags=XP_UI_TOUCH_HELD;a.loop();input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();assert(a.actions==previous);}
  input={};a.loop();input.released=confirm;a.loop();assert(a.actions==previous+1);
}
