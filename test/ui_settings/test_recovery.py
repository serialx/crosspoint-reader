"""Exercise the production settings arena and failed-scene routing with fault injection."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'src/activities/settings/SettingsUiActivity.cpp'
source = source_path.read_text()

def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <xp_ui_settings.h>
#include <cassert>
#include <cstdint>
#include <memory>
#define LOG_ERR(...) ((void)0)
#ifndef XP_UI_SETTINGS_SMALL_LABELS_ON_TOUCH
#define XP_UI_SETTINGS_SMALL_LABELS_ON_TOUCH 4
#endif
constexpr int UI_12_FONT_ID=1, UI_10_FONT_ID=0;
static bool failGrowth=false;
static unsigned growthCalls=0;
static XpUiInput forwarded{};
template<class T> std::unique_ptr<T> makeUniqueNoThrow(unsigned n) {
  if(n==64) {++growthCalls;if(failGrowth)return {};}
  return std::unique_ptr<T>(new typename std::remove_extent<T>::type[n]);
}
extern "C" XpUiEffect xp_ui_settings_list_update(XpUiListState*,const XpUiInput* i,uint16_t) {
  forwarded=*i;
  XpUiEffect e{};
  if(i->released&XP_UI_BACK)e.kind=XP_UI_CANCEL;
  else if(i->released&XP_UI_CONFIRM)e.kind=XP_UI_ACTIVATE;
  return e;
}
extern "C" uint8_t xp_ui_settings_hit(const XpUiSettingsHits*,int16_t,int16_t,XpUiInteraction*) {return 0;}
extern "C" uint8_t xp_ui_settings_view(const XpUiListState*,const XpUiSettingsLayout*,const XpUiSettingsResources*,
  const XpUiBuffer* b,XpUiListState*,XpUiScene*,XpUiSettingsHits*) {return b->command_capacity<64?2:1;}
extern "C" void xp_ui_settings_list_publish(XpUiListState*,const XpUiListState*,int32_t) {}
struct Rect {int x=0,y=0,width=480,height=800;};
struct Metrics {
  int listRowHeight=44,listRowGap=0,listInset=0,listSidePadding=0,listScrollWidth=5,listRowRadius=0,
      listSelectionStyle=0,listScrollSide=0,listTitleBold=0,topPadding=0,headerHeight=40,buttonHintsHeight=40,
      verticalSpacing=8,tabBarHeight=30,contentSidePadding=5;
};
struct Renderer {int getScreenWidth(){return 480;}int getScreenHeight(){return 800;}};
struct UITheme {
  static UITheme& getInstance(){static UITheme t;return t;}
  Metrics getMetrics(){return {};}
  Rect getScreenSafeArea(Renderer&,bool,bool){return {};}
  int getStatusBarHeight(){return 20;}
};
struct Fonts {int bodyFontId=1,smallFontId=0;};
Fonts uiScaleSpec(){return {};}
class SettingsUiActivity {
public:
  struct ModelLock {ModelLock(SettingsUiActivity&) {}};
  struct Device {int16_t width=480,height=800;struct {int16_t left=0,top=0,right=0,bottom=0;}safeArea;};
  struct Target {Device deviceContext(){return {};}}textTarget;
  struct Input {bool hasTouch(){return true;}}mappedInput;
  Renderer renderer;
  struct {bool ready=false;}publication;
  XpUiSettingsKind kind=XP_UI_SETTINGS_SECTION;
  XpUiListState state{},next{};
  XpUiSettingsHits publishedHits{},nextHits{};
  XpUiSettingsLayout layout{};
  std::unique_ptr<XpUiCommand[]> commands;
  uint16_t commandCapacity=0,requestedCapacity=0;
  int renderingScroll=0,touchX=0,touchY=0;
  bool sceneFailed=false,touchActivated=false,longTouch=false;
  int listCount(){return 12;}
  void prepareRows(){}
  bool smallLabelsOnTouch(){return true;}
  uint8_t screenFlags(){return 0;}
  XpUiSettingsResources resources(){return {};}
  bool reserveCommands(uint16_t);
  bool buildRustScene(XpUiScene&);
  XpUiEffect updateRust(const XpUiInput&);
  void rustPublished();
};
'''
code += '\n'.join(method(signature) for signature in [
    'bool SettingsUiActivity::reserveCommands(',
    'bool SettingsUiActivity::buildRustScene(',
    'XpUiEffect SettingsUiActivity::updateRust(',
    'void SettingsUiActivity::rustPublished('])
code += r'''
int main() {
  SettingsUiActivity activity;
  XpUiInput confirm{};confirm.released=XP_UI_CONFIRM;
  // Original key activation is allowed before the first paint.
  assert(activity.updateRust(confirm).kind==XP_UI_ACTIVATE);
  assert(activity.reserveCommands(48));
  failGrowth=true;
  XpUiScene scene{};
  assert(!activity.buildRustScene(scene));
  assert(activity.sceneFailed&&!activity.commands&&activity.requestedCapacity==64);
  assert(growthCalls==1);
  assert(activity.updateRust(confirm).kind==XP_UI_EFFECT_NONE);
  XpUiInput back{};back.released=XP_UI_BACK;
  assert(activity.updateRust(back).kind==XP_UI_CANCEL);
  failGrowth=false;
  assert(activity.buildRustScene(scene));
  assert(activity.commands&&activity.commandCapacity==64&&growthCalls==2);
  assert(activity.layout.flags==4);
  // A built frame alone cannot authorize stale-scene activation.
  assert(activity.updateRust(confirm).kind==XP_UI_EFFECT_NONE);
  activity.rustPublished();
  assert(activity.updateRust(confirm).kind==XP_UI_ACTIVATE);
  assert(!activity.reserveCommands(129));
  assert(activity.commands&&activity.commandCapacity==64);
}
'''
with tempfile.TemporaryDirectory(prefix='settings-recovery-') as tmp:
    p = Path(tmp)
    (p/'main.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(p/'main.cpp'),'-o',str(p/'check')],check=True)
    subprocess.run([str(p/'check')],check=True)
print('settings growth failure, Back, retry and publish gating passed')
