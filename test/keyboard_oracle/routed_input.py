"""Compare original routing and current facade with real SDK layouts."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import record
from layout_instrument import function

root = record.ROOT
archive = Path(sys.argv[1])
s = record.source(root).split('void hex(')[0]
s = s.replace('struct GfxRenderer {};', '''
struct EpdFontFamily { enum { REGULAR }; };
constexpr int UI_12_FONT_ID=1;
struct GfxRenderer {
 int getScreenWidth() const {return 528;} int getScreenHeight() const {return 792;}
 int getLineHeight(int) const {return 20;}
 int getTextAdvanceX(int,const char* text,int) const {
  int width=0;for(const unsigned char* p=(const unsigned char*)text;*p;++p) if((*p&0xc0)!=0x80)width+=8;
  return width;
 }
 int getTextWidth(int font,const char* text) const {return getTextAdvanceX(font,text,0);}
};
struct BidiUtils {
 static int detectParagraphLevel(const char* s,int base,int depth) {
  return xp_bidi_paragraph_level((const uint8_t*)s,strlen(s),base,depth);
 }
};
struct Rect { int x=0,y=0,width=528,height=792; };
''')
s = re.sub(r'struct ThemeMetrics\s*\{[^}]*\};', '''
struct ThemeMetrics {
 int keyboardKeySpacing=6,keyboardKeyHeight=56,topPadding=0,headerHeight=40,verticalSpacing=8,
 keyboardVerticalOffset=0,sideButtonHintsWidth=0,keyboardTextFieldWidthPercent=80,buttonHintsHeight=40;
 bool keyboardCenteredText=false;
};
struct UITheme {
 static UITheme& getInstance(){ static UITheme theme;return theme; }
 const ThemeMetrics& getMetrics() const {static ThemeMetrics metrics;return metrics;}
 static Rect getContentArea(const GfxRenderer&){return {};}
};
struct { bool deviceIsX3() const {return false;} } gpio;
''', s, count=1)
s = s.replace('  bool wasPressed(Button b)', '  bool hasTouch() const {return true;}\n  bool wasPressed(Button b)')
s = s.replace('void KeyboardEntryActivity::render(RenderLock&&) {}', '')
s = s.replace('bool KeyboardEntryActivity::cursorPositionFromPoint(int, int, size_t&) const { return false; }', '')
baseline = record.baseline('src/activities/util/KeyboardEntryActivity.cpp')
start = baseline.index('int KeyboardEntryActivity::measureRange(')
end = baseline.index('void KeyboardEntryActivity::loop()')
s += baseline[start:end]
s = s.replace('KeyboardEntryActivity', 'OriginalKeyboard')
current = (root/'test/keyboard_oracle/Allocation.cpp').read_text().split('int main()')[0]
# The input source, scheduler and result recipient are host leaves. Keyboard
# loop/update/growth/result/publication bodies are the actual production methods.
current = current.replace('  void loop() { ++baseLoops; }', '  void loop();')
a = current.index('  struct Input {');b = current.index('  void loop();',a)
current = current[:a]+'  MappedInputManager mappedInput;\n'+current[b:]
a = function(current,'  static XpUiKeyboardMetrics metrics()')
current = current.replace(a,'''  static XpUiKeyboardMetrics metrics() {
 return {nullptr,[](void*,int32_t font,const uint8_t* s,uint32_t n,uint8_t mode)->int32_t {
  if(mode==2)return xp_bidi_paragraph_level(s,n,0,n);
  return GfxRenderer{}.getTextAdvanceX(font,std::string((const char*)s,n).c_str(),0);
 },[](void*,int32_t)->int32_t{return 20;}};
}''')
a = function(current,'  static XpUiKeyboardLayouts layouts()')
callbacks = function((root/'src/activities/util/KeyboardEntryActivity.cpp').read_text(), 'XpUiKeyboardLayouts KeyboardEntryActivity::layouts(')
current = current.replace(a,'  static XpUiKeyboardLayouts layouts();')
current += callbacks
current += '''
void RustActivityAdapter::loop() {
 auto& self=*static_cast<KeyboardEntryActivity*>(this);
 const auto& i=self.mappedInput; XpUiInput input{};
 constexpr uint32_t buttons[]{XP_UI_BACK,XP_UI_CONFIRM,XP_UI_LEFT,XP_UI_RIGHT,XP_UI_UP,XP_UI_DOWN,XP_UI_NAV_NEXT,XP_UI_NAV_PREVIOUS};
 for(unsigned b=0;b<8;++b) {
  if(i.pressed&(1u<<b))input.pressed|=buttons[b];
  if(i.released&(1u<<b))input.released|=buttons[b];
  if(i.held&(1u<<b))input.held|=buttons[b];
 }
 input.now_ms=millis();input.held_ms=i.duration;input.touch_x=i.x;input.touch_y=i.y;
 input.touch_flags=(i.down?XP_UI_TOUCH_PRESSED:0)|(i.tapped?XP_UI_TOUCH_RELEASED:0)|(i.contact?XP_UI_TOUCH_HELD:0);
 self.step(input);
}
'''
# Include standard headers outside the candidate namespace.
source = '#include <xp_ui_keyboard.h>\n#include <xp_ffi.h>\n#include <memory>\n#include <cassert>\n'+s+'\nnamespace candidate {\n'+current+'\n}\n'
source += (root/'test/keyboard_oracle/RoutedInput.cpp').read_text()
with tempfile.TemporaryDirectory() as tmp:
    out=Path(tmp)
    subprocess.run([sys.executable,str(root/'test/keyboard_oracle/allocation_instrument.py'),str(root),str(out/'keyboard-allocation.inc')],check=True)
    (out/'routed.cpp').write_text(source)
    subprocess.run(['c++','-std=c++20','-O1','-DFREEINK_UI_COMPONENT_STYLE=0',
                    '-I'+str(out),'-I'+str(record.SDK/'include'),'-I'+str(root/'lib/I18n'),
                    '-I'+str(root/'rust/crates/xp-ffi/include'),str(out/'routed.cpp'),
                    str(record.SDK/'src/FreeInkUI.cpp'),str(root/'test/cmake/xp_ffi_host.cpp'),
                    str(archive),'-o',str(out/'routed')],check=True)
    subprocess.run([str(out/'routed')],check=True)
