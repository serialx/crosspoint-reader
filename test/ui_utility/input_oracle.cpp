// Frozen application methods from ba6860d8; deterministic host leaves only.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <utility>
struct MappedInputManager {
 enum class Button {Back,Confirm,NavNext,NavPrevious,Left,Right,Up,Down};
 enum class SwipeDir {None,Up,Down,Left,Right};
 uint32_t pressed=0,released=0,held=0,heldMs=0; SwipeDir swipe=SwipeDir::None;
 bool wasPressed(Button b)const{return pressed&(1u<<unsigned(b));}
 bool wasReleased(Button b)const{return released&(1u<<unsigned(b));}
 bool isPressed(Button b)const{return held&(1u<<unsigned(b));}
 uint32_t getHeldTime()const{return heldMs;}
 SwipeDir wasSwipe()const{return swipe;}
} input;
uint32_t nowMs=0;uint32_t millis(){return nowMs;}
struct ButtonNavigator {
 using Callback=std::function<void()>;using Buttons=std::initializer_list<MappedInputManager::Button>;
 static const MappedInputManager* mappedInput;uint32_t lastContinuousNavTime=0;uint16_t continuousStartMs=500,continuousIntervalMs=500;
 bool shouldNavigateContinuously()const;void onPressAndContinuous(const Buttons&,const Callback&);
 static void onPress(const Buttons&,const Callback&);void onContinuous(const Buttons&,const Callback&);
};const MappedInputManager* ButtonNavigator::mappedInput=&input;
struct ActivityResult {bool isCancelled=false;};struct IntervalResult {uint32_t value;};
struct Route {
 struct Event {int16_t dragPermille=-1;int value=0;}event;
 struct Snap {bool touchHeld=false,touchReleased=false;int touchX=-1,touchY=-1;}snap;
 bool routed=false,handled=false;explicit operator bool()const{return handled;}
};
struct Base {
 MappedInputManager& mappedInput=input;Route route;int paints=0,done=0,result=0;bool ready=false;
 Route routeTouch(MappedInputManager&,bool=false,bool=false){return route;}
 bool routingReady()const{return ready;}void requestUpdate(){++paints;}void finish(){++done;}
 void setResult(ActivityResult r){result=r.isCancelled?-1:1;}void setResult(IntervalResult r){result=int(r.value);}
 struct App {bool invalid=false;bool invalidated()const{return invalid;}}app;
};
struct {bool edge=false;bool hasEdgeSideButtons()const{return edge;}}gpio;
struct IntervalSelectionActivity:Base {
 int value=20,minValue=0,maxValue=100,smallStep=1,largeStep=10;ButtonNavigator buttonNavigator;bool draggingSlider=false;
 int clampedValue(int)const;void adjustValue(int);void setValue(int);void cancel();void confirm();void loop();
};
constexpr int MIN_BRIGHTNESS=1,BRIGHTNESS_STEP=1;
struct CrossPointSettings {static constexpr uint8_t TOUCH_READER_OFF=0;};
struct {uint8_t frontlightBrightness=60,frontlightWarmth=50,frontlightOn=0,screenInverted=0,orientation=0,touchReaderControls=2;int saves=0;void saveToFile(){++saves;}}SETTINGS;
struct {int brightness=60,warmth=50;bool on=false;int bwrites=0,wwrites=0,owrites=0;
 void setBrightness(int v){brightness=v;++bwrites;}void setWarmth(int v){warmth=v;++wwrites;}void setOn(bool v){on=v;++owrites;}}Frontlight;
bool switchOwns=false;bool slideSwitchOwnsLight(){return switchOwns;}
struct HalDisplay {enum RefreshMode {FULL_REFRESH};};
struct FrontlightPanelActivity:Base {
 uint8_t brightness=60,warmth=50,touchModeRestore=2;bool lightOn=false,lightOnChanged=false,draggingSlider=false,cleanRefreshPending=false;int panelBottom=300;
 ButtonNavigator buttonNavigator;
 struct Renderer {int promoted=0;void promoteNextRefresh(HalDisplay::RefreshMode){++promoted;}}renderer;
 void persistLightSettings();void runTile(int);void adjustBrightness(int);void adjustWarmth(int);void toggleLight();void close();void loop();
};
void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}
void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}
void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  const bool wasPressedOrReleased =
      std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
        return mappedInput != nullptr && (mappedInput->wasPressed(button) || mappedInput->wasReleased(button));
      });
  if (wasPressedOrReleased) {
    lastContinuousNavTime = 0;
    return;  // A press already stepped once; a release must never repeat.
  }
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously();
  });

  if (isPressed) {
    callback();
    lastContinuousNavTime = millis();
  }
}
bool ButtonNavigator::shouldNavigateContinuously() const {
  if (!mappedInput) return false;

  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}
int IntervalSelectionActivity::clampedValue(const int candidate) const {
  return std::clamp(candidate, minValue, maxValue);
}
void IntervalSelectionActivity::adjustValue(const int delta) {
  value = clampedValue(value + delta);
  requestUpdate();
}
void IntervalSelectionActivity::setValue(const int candidate) {
  const int clamped = clampedValue(candidate);
  if (clamped == value) return;
  value = clamped;
  requestUpdate();
}
void IntervalSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
void IntervalSelectionActivity::confirm() {
  setResult(IntervalResult{static_cast<uint32_t>(value)});
  finish();
}
void IntervalSelectionActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the slider, -/+ zones,
  // and Cancel/OK hit rects; the slider follows the finger via InputDrag. Runs
  // before the Back handler because the release of a drag can also register as a
  // swipe (e.g. the left-edge rightward back gesture) — the drag must consume it
  // so it can't cancel the dialog.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    // Drag ended (possibly off the slider): swallow the tap/swipe events it produced.
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }
  // Tap released outside the dialog (inside-taps are absorbed by the chrome
  // guard): cancel. Swipe-end releases arrive with -1,-1 coords and fall
  // through — same rule as OptionPopup.
  if (route.routed && route.snap.touchReleased && route.snap.touchX >= 0) {
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustValue(-smallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustValue(smallStep); });

  // On edge-button boards (X3, X4 Pro) the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left button and BTN_DOWN the right
  // one. Flip the large-step direction there so the left button decreases and the right button increases.
  const int upDelta = gpio.hasEdgeSideButtons() ? -largeStep : largeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? largeStep : -largeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { adjustValue(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { adjustValue(downDelta); });
}
void FrontlightPanelActivity::persistLightSettings() {
  // brightness/warmth are always restored unconditionally on boot (see
  // main.cpp), so they never diverge from SETTINGS at onEnter() — comparing
  // against SETTINGS here only fires on a genuine user change. lightOn has
  // no such guarantee (see lightOnChanged's declaration), so it's gated on
  // the user actually having touched it this session instead.
  const bool changed = SETTINGS.frontlightBrightness != brightness || SETTINGS.frontlightWarmth != warmth ||
                       (lightOnChanged && SETTINGS.frontlightOn != (lightOn ? 1 : 0));
  if (changed) {
    SETTINGS.frontlightBrightness = brightness;
    SETTINGS.frontlightWarmth = warmth;
    if (lightOnChanged) SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
  }
}
void FrontlightPanelActivity::runTile(const int idx) {
  switch (idx) {
    case 0:  // Night mode (inverted output polarity, applied to the whole UI)
      SETTINGS.screenInverted = SETTINGS.screenInverted ? 0 : 1;
      SETTINGS.saveToFile();
      // Inversion rewrites every pixel; take the clean waveform so the panel
      // does not keep a ghost of the old polarity.
      cleanRefreshPending = true;
      requestUpdate();
      break;
    case 1:  // Ghost-cleanup refresh of the whole frame
      // Refreshing with the panel still up would clean a frame the user is
      // about to dismiss anyway: drop the panel first and let the repaint of
      // the screen underneath carry the clean waveform instead.
      renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
      close();
      break;
    case 2:  // Cycle the reading orientation
      SETTINGS.orientation = static_cast<uint8_t>((SETTINGS.orientation + 1) % 4);
      SETTINGS.saveToFile();
      // Only the setting changes: turning the renderer cropped the portrait-only
      // screens the panel opens over. The reader reflows on its next loop().
      requestUpdate();
      break;
    case 3:  // Touch reader controls (for reading with the palm on the glass)
      // Toggles the existing Settings -> Controls option, nothing lower-level:
      // that setting only governs the reader's tap/swipe handling, so the
      // panel's own gestures (including the swipe that reopens it) keep
      // working while it is off. Off remembers the mode (Tap/Swipe/Inverted
      // Tap) so toggling back does not stomp the user's choice.
      if (SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF) {
        touchModeRestore = SETTINGS.touchReaderControls;
        SETTINGS.touchReaderControls = CrossPointSettings::TOUCH_READER_OFF;
      } else {
        SETTINGS.touchReaderControls = touchModeRestore;
      }
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    default:
      break;
  }
}
void FrontlightPanelActivity::adjustBrightness(const int delta) {
  int next = static_cast<int>(brightness) + delta;
  if (next < MIN_BRIGHTNESS) next = MIN_BRIGHTNESS;
  if (next > 100) next = 100;
  if (next == brightness) return;
  brightness = static_cast<uint8_t>(next);
  Frontlight.setBrightness(brightness);
  if (!lightOn && !slideSwitchOwnsLight()) {
    lightOn = true;
    lightOnChanged = true;
    Frontlight.setOn(true);
  }
  requestUpdate();
}
void FrontlightPanelActivity::adjustWarmth(const int delta) {
  int next = static_cast<int>(warmth) + delta;
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  if (next == warmth) return;
  warmth = static_cast<uint8_t>(next);
  Frontlight.setWarmth(warmth);
  requestUpdate();
}
void FrontlightPanelActivity::toggleLight() {
  lightOn = !lightOn;
  lightOnChanged = true;
  Frontlight.setOn(lightOn);
  requestUpdate();
}
void FrontlightPanelActivity::close() { finish(); }
void FrontlightPanelActivity::loop() {
  const auto touch = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (touch.routed) {
    if (app.invalidated()) requestUpdate();
    if (touch) {
      if (touch.event.dragPermille >= 0) draggingSlider = true;
      return;
    }
    // Swipe up dismisses the sheet, the way it was pulled down from the top
    // edge. draggingSlider keeps a fast slider flick from closing it.
    if (!draggingSlider && mappedInput.wasSwipe() == MappedInputManager::SwipeDir::Up) {
      close();
      return;
    }
    // panelBottom > 0 guards the frame the sheet opens in: the release that
    // opened it (a status-bar tap) is still in the input snapshot when the panel
    // runs its first loop(), and panelBottom is only known once render() has
    // measured the layout — so at 0 that release read as "tapped below the
    // sheet" and closed it again before it was ever drawn.
    if (touch.snap.touchReleased && !draggingSlider && panelBottom > 0 && touch.snap.touchY >= panelBottom) {
      close();
      return;
    }
  }
  if (draggingSlider) {
    if (!touch.snap.touchHeld) {
      draggingSlider = false;
      cleanRefreshPending = true;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!slideSwitchOwnsLight()) toggleLight();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this] { adjustBrightness(-BRIGHTNESS_STEP); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this] { adjustBrightness(BRIGHTNESS_STEP); });
}

int main(){
 for(bool edge:{false,true})for(unsigned button=0;button<8;++button){
  IntervalSelectionActivity a;gpio.edge=edge;input={};nowMs=1000;
  for(int phase=0;phase<6;++phase){
   input.pressed=phase==0?1u<<button:0;input.released=phase==5?1u<<button:0;input.held=phase<5?1u<<button:0;
   input.heldMs=phase*250;nowMs=1000+phase*250;a.loop();
   std::printf("interval %d %u %d %d %d %d %d\n",edge,button,phase,a.value,a.paints,a.done,a.result);
  }
 }
 for(bool own:{false,true})for(int start:{1,60,100})for(unsigned button:{0u,1u,4u,5u}){
  FrontlightPanelActivity a;a.brightness=start;switchOwns=own;input={};Frontlight.bwrites=0;Frontlight.owrites=0;
  for(int phase=0;phase<6;++phase){input.pressed=phase==0?1u<<button:0;input.released=phase==5?1u<<button:0;input.held=phase<5?1u<<button:0;input.heldMs=phase*250;nowMs=1000+phase*250;a.loop();
   std::printf("light %d %d %u %d %u %d %d %d %d %d %d\n",own,start,button,phase,a.brightness,a.lightOn,a.lightOnChanged,a.paints,a.done,Frontlight.bwrites,Frontlight.owrites);
  }
 }
 for(int tile=0;tile<4;++tile){FrontlightPanelActivity a;SETTINGS.screenInverted=0;SETTINGS.orientation=3;SETTINGS.touchReaderControls=2;SETTINGS.saves=0;
  for(int n=0;n<2;++n){a.runTile(tile);std::printf("tile %d %d %u %u %u %u %d %d %d %d %d\n",tile,n,SETTINGS.screenInverted,SETTINGS.orientation,SETTINGS.touchReaderControls,a.touchModeRestore,SETTINGS.saves,a.cleanRefreshPending,a.renderer.promoted,a.done,a.paints);}}
 for(bool dragged:{false,true})for(int kind=0;kind<3;++kind){FrontlightPanelActivity a;input={};a.draggingSlider=dragged;a.ready=true;a.route.routed=true;
 a.route.snap.touchReleased=true;a.route.snap.touchX=20;a.route.snap.touchY=kind==0?400:20;input.swipe=kind==1?MappedInputManager::SwipeDir::Up:MappedInputManager::SwipeDir::None;
 a.loop();std::printf("dismiss %d %d %d %d %d %d\n",dragged,kind,a.done,a.draggingSlider,a.cleanRefreshPending,a.paints);}
}
