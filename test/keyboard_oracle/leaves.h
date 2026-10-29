#include <FreeInkUI.h>
#include <I18nKeys.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>

uint32_t now = 0;
uint32_t millis() { return now; }
struct Settings { uint16_t keyboardLayouts = 0; } SETTINGS;
struct Locale {
  Language language = Language::EN;
  Language getLanguage() const { return language; }
} I18N;
struct RenderLock {};
struct GfxRenderer {};
struct ThemeMetrics { int keyboardKeySpacing=6, keyboardKeyHeight=56; };
class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, NavNext, NavPrevious };
  uint32_t pressed=0, released=0, held=0, duration=0;
  bool down=false, tapped=false, contact=false;
  int x=0, y=0;
  bool wasPressed(Button b) const { return pressed & (1u << unsigned(b)); }
  bool wasReleased(Button b) const { return released & (1u << unsigned(b)); }
  bool isPressed(Button b) const { return held & (1u << unsigned(b)); }
  uint32_t getHeldTime() const { return duration; }
  bool wasScreenTapped(int& tx, int& ty) const { tx=x;ty=y;return tapped; }
  bool wasScreenTouchDown(int& tx, int& ty) const { tx=x;ty=y;return down; }
  bool isScreenTouchHeld(int& tx, int& ty) const { tx=x;ty=y;return contact; }
};
struct KeyboardResult { std::string text; };
struct ActivityResult { bool isCancelled=false; };
struct Activity {
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  unsigned repaints=0;
  int result=0;
  std::string returned;
  Activity(const char*, GfxRenderer& r, MappedInputManager& i):renderer(r),mappedInput(i){}
  virtual ~Activity()=default;
  virtual void onEnter(){}
  virtual void onExit(){}
  virtual void loop(){}
  virtual void render(RenderLock&&){}
  virtual bool preventAutoSleep(){ return false; }
  void requestUpdate(){++repaints;}
  void finish(){}
  void setResult(KeyboardResult r){result=1;returned=std::move(r.text);}
  void setResult(ActivityResult r){result=r.isCancelled ? 2 : 0;}
};
namespace haptic_feedback { void touchAction(bool=false){} }
