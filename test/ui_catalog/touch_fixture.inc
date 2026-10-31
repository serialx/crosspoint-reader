// Hardware samples are injected below the SDK contact classifier. The SDK,
// mapped-input queries, snapshot adapter, and catalog dispatch are production.
#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "FreeInkUICore.h"
#include "xp_ui_catalog.h"
#define FREEINK_CAP_TOUCH 1
unsigned long clockMs=0;
unsigned long millis(){return clockMs;}
namespace BoardConfig {
enum class TouchController {None};
struct {struct {TouchController controller=TouchController::None;bool synthesizeConfirm=false;} touch;} ACTIVE;
}
void xQueueReset(void*){}
constexpr unsigned TOUCH_LONG_PRESS_MS=500,TOUCH_DOWN_SELECT_DELAY_MS=90;
constexpr int BTN_CONFIRM=0;
struct InputManager {
 struct Point {uint16_t x=0,y=0;unsigned long timestamp=0;} touchDownPoint,touchUpPoint;
 bool touchDataEnabled=true,touchPressed=false,touchReleasedEvent=false,touchSuppressed=false;
 bool touchLongPressFired=false,touchLongPressEvent=false,touchMultiContactSequence=false;
 bool touchMovedBeyondTapSlop=false,touchMovedBeyondTapReleaseSlop=false;
 unsigned long touchIrqPulseUntil=0;
 void* _asyncMultiTouchSwipeQueue=nullptr;void* _asyncMultiTouchRotationQueue=nullptr;
 void* _asyncMultiTouchPinchQueue=nullptr;
 void resetMultiTouchGesture(){}void cancelMultiTouchGesture(){}
 void normalizeTouchPoint(uint16_t x,uint16_t y,float& nx,float& ny)const {nx=x;ny=y;}
 uint8_t serviceTouch();bool wasTouchTap(float&,float&)const;bool wasTouchLongPress(float&,float&)const;
 void suppressTouchContact();bool isTouchTapCandidate(float&,float&,unsigned long&)const;
 bool isTouchHeldAt(float&,float&)const;bool wasTouchReleased()const;
 void sample(unsigned long now,bool down,int x){
  clockMs=now;touchLongPressEvent=false;
  touchReleasedEvent=touchPressed && !down;
  if(down && !touchPressed)touchDownPoint={static_cast<uint16_t>(x),20,now};
  touchUpPoint=touchDownPoint;touchPressed=down;serviceTouch();
 }
};
// SDK_METHODS
struct HeaderBackTapTarget {static bool contains(int,int){return false;}};
struct MappedInputManager {
 enum class Button {Back,Confirm,NavPrevious,NavNext};
 InputManager& gpio;
 struct {void tapToLogical(float x,float y,int& tx,int& ty)const {tx=x;ty=y;}} renderer;
 void rememberTouchHeldTime()const {}
 bool wasScreenTapped(int&,int&)const;bool wasScreenLongPress(int&,int&)const;
 bool wasScreenTouchDown(int&,int&)const;bool isScreenTouchHeld(int&,int&)const;
 bool wasScreenTouchReleased()const;
 bool wasReleased(Button)const{return false;}bool wasPressed(Button)const{return false;}
 bool isPressed(Button)const{return false;}
};
// MAPPED_METHODS
// SNAPSHOT_METHOD
namespace haptic_feedback {void touchAction() {}}
struct Popup {
 bool active=false,uiReady=true;int selectedIndex=0;
 static constexpr int ACTION_OPTION=1,ACTION_CHROME=2;
 freeink::ui::InteractionBuffer<4> interactions;
 std::function<void(int)> onSelectCallback;
 bool handleInput(MappedInputManager&,const std::function<void()>&);
 bool handleButtons(bool,bool,bool,bool,const std::function<void()>&){return true;}
};
struct OriginalBase {bool consumeRejectedInput(){return false;}bool handleCustomInput(){return false;}};
using CatalogActivity=OriginalBase;
struct Original:OriginalBase {
 enum class State {CHECK_WIFI,WIFI_SELECTION,SEARCH_INPUT,LOADING,BROWSING,DOWNLOADING,
                  ERROR,PLUGIN_PICKER,LIST_PICKER,DONE,NO_TOKEN,AUTH};
 State state=State::PLUGIN_PICKER;
 MappedInputManager mappedInput;Popup pluginHubPopup;
 bool ignoreHubInstallerBackRelease=false;unsigned long authNextPollMs=9999;
 int actions=0,value=-1,uninstalls=0;
 explicit Original(InputManager& i):mappedInput{i}{}
 bool handleCustomInput();void requestUpdate(){}void pollAuth(){assert(false);}
 void exitCatalog(){++actions;}bool wifiConnected(){return true;}
 void launchWifiSelection(){assert(false);}void retryBrowse(){++actions;}
 void computeInstallStatus(){++actions;}void releaseRows(){}
 void loop(){if(handleCustomInput())return;
  const auto snap=touchSnapshotFrom(mappedInput,true);
  if(snap.longPress)++uninstalls;else if(snap.touchReleased)++actions;
 }
};
#include "touch_original.inc"
struct CatalogUiActivity;
struct ModelLock {explicit ModelLock(CatalogUiActivity&) {}};
struct RustActivityAdapter {virtual void normalInput()=0;void loop(){normalInput();}};
struct CatalogUiActivity:RustActivityAdapter {
 XpUiCatalogState ui{};XpUiCatalogHits publishedHits{};
 bool sceneFailed=false,touchActivated=false,rejectedTouch=false;uint16_t rejectedButtons=0;
 struct {bool ready=true;} publication;
 MappedInputManager mappedInput;
 int actions=0,value=-1,uninstalls=0;
 explicit CatalogUiActivity(InputManager& i):mappedInput{i}{
  xp_ui_catalog_init(&ui,XP_UI_CATALOG_PLUGINS,-1,0);ui.mode=XP_UI_CATALOG_PLUGIN_PICKER;
 }
 bool consumeRejectedInput(){return false;}bool handleCustomInput(){return false;}void loop();XpUiEffect updateRust(const XpUiInput&);
 int listCount(){return 1;}bool hasSearch(){return false;}uint8_t pageLinks(){return 0;}
 void requestUpdate(){}
 void applyRustEffect(const XpUiEffect& e){
  if(e.kind==XP_UI_CATALOG_UNINSTALL)++uninstalls;
  else if(e.kind){++actions;value=e.value;}
 }
 void normalInput()override {
  const auto snap=touchSnapshotFrom(mappedInput);
  XpUiInput i{};i.touch_x=snap.touchX;i.touch_y=snap.touchY;
  i.touch_flags=(snap.touchPressed?XP_UI_TOUCH_PRESSED:0)|(snap.touchReleased?XP_UI_TOUCH_RELEASED:0)|
                (snap.touchHeld?XP_UI_TOUCH_HELD:0);
  applyRustEffect(updateRust(i));
 }
};
// CATALOG_METHODS
int main(){
 // Each option, outside the popup, DONE, NO_TOKEN, and picker hold.
 for(int scenario=0;scenario<7;++scenario){
  InputManager oldInput,newInput;Original old(oldInput);CatalogUiActivity candidate(newInput);
  const int x=scenario<3?20+scenario*100:scenario==3?400:20;
  if(scenario<4){
   old.pluginHubPopup.active=true;candidate.ui.popup=1;
   old.pluginHubPopup.onSelectCallback=[&](int value){++old.actions;old.value=value;};
   old.pluginHubPopup.interactions.beginPublishCycle();
   for(int n=0;n<3;++n){
    freeink::ui::Interaction hit{};hit.rect={static_cast<int16_t>(n*100),0,80,80};
    hit.action=1;hit.value=n;hit.inputMask=freeink::ui::InputTouch;
    old.pluginHubPopup.interactions.addInteraction(hit);
    candidate.publishedHits.controls[n]={{static_cast<int16_t>(n*100),0,80,80},XP_UI_CATALOG_HUB_OPTION,n,1,0};
   }
   old.pluginHubPopup.interactions.publish();candidate.publishedHits.control_count=3;
  }else if(scenario<6){
   old.state=scenario==4?Original::State::DONE:Original::State::NO_TOKEN;
   candidate.ui.mode=scenario==4?XP_UI_CATALOG_DONE:XP_UI_CATALOG_NO_TOKEN;
  }else{
   candidate.publishedHits.rows=1;candidate.publishedHits.row_area={0,0,100,100};
   candidate.publishedHits.heights[0]=100;
  }
  for(unsigned ms:{0,90,499,500,600,601}){
   const bool down=ms<601;oldInput.sample(ms,down,x);newInput.sample(ms,down,x);
   old.loop();candidate.loop();
   if(ms==500 && scenario==6)assert(old.uninstalls==1 && candidate.uninstalls==1);
  }
  std::fprintf(stderr,"case %d actions %d/%d value %d/%d uninstall %d/%d\n",scenario,old.actions,candidate.actions,old.value,candidate.value,old.uninstalls,candidate.uninstalls);
  assert(old.actions==candidate.actions && (scenario>=3 || old.value==candidate.value));
  assert(old.uninstalls==candidate.uninstalls);
  if(scenario<3 || scenario==4 || scenario==5)assert(candidate.actions==1);
  if(scenario<4)assert(!old.pluginHubPopup.active && !candidate.ui.popup);
  if(scenario==6)assert(candidate.actions==0 && candidate.uninstalls==1);
 }
 // Physical hold uses the original 1000 ms threshold, independently of touch.
 for(unsigned ms:{999,1000}){
  InputManager raw;CatalogUiActivity a(raw);XpUiInput i{};
  i.pressed=XP_UI_CONFIRM;a.updateRust(i);i={};i.held=XP_UI_CONFIRM;i.held_ms=ms;
  a.applyRustEffect(a.updateRust(i));i={};i.released=XP_UI_CONFIRM;a.applyRustEffect(a.updateRust(i));
  assert(a.uninstalls==(ms==1000) && a.actions==(ms==999));
 }
}
