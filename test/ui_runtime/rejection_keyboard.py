"""Run production keyboard edits, cursor taps and rendering through the real gate."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'test/keyboard_oracle/Allocation.cpp').read_text().split('int main() {', 1)[0]
a = source.index('struct RustActivityAdapter {')
b = source.index('class KeyboardEntryActivity', a)
source = source[:a] + source[b:]
source = source.replace('#define LOG_ERR', '#undef LOG_ERR\n#define LOG_ERR')
source = '#include "RustActivityAdapter.h"\n' + source
source = source.replace('class KeyboardEntryActivity : public RustActivityAdapter {\n public:', '''class KeyboardEntryActivity : public RustActivityAdapter {
 public:
  KeyboardEntryActivity(GfxRenderer& r, MappedInputManager& i):RustActivityAdapter("keyboard",r,i){}
  void rustSceneFailed() override {resetInput=true;}
  Publication* rustPublication() override {return &publication;}
  XpUiInterest rustInterest(uint32_t) const override {return inputInterest;}
''')
# Reuse the production adapter Publication instead of the standalone allocation leaf.
a = source.index('  struct Publication {')
b = source.index('  XpUiKeyboardState publishedState', a)
source = source[:a] + '  Publication publication;\n' + source[b:]
a = source.index('  struct Input {')
b = source.index('  void loop();', a)
source = source[:a] + source[b:]
source += r'''
int main(){
 for(bool first:{false,true})for(bool buildFailure:{false,true}){
  GfxRenderer renderer;renderer.finishRefresh=true;MappedInputManager input;
  KeyboardEntryActivity a(renderer,input);
  assert(a.text.grow(64,0));memcpy(a.text.data.get(),"seed",5);
  xp_ui_keyboard_init(&a.state,4,0,0,1,0);a.inputInterest=xp_ui_keyboard_interest(&a.state);
  a.arena=std::make_unique<KeyboardEntryActivity::Arena>();
  auto& l=a.layout;l.screen={0,0,480,800};l.content=l.screen;l.font=1;
  l.spacing=8;l.header_height=40;l.key_height=56;l.hints_height=40;
  l.key_spacing=6;l.text_percent=80;l.field_normal=2;l.field_cursor=3;l.field_end=-1;
  auto paint=[&]{RenderLock lock;a.render(std::move(lock));};
  // Pre-first-paint input still edits, using the actual adapter and keyboard.
  input.pressed=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
  input.pressed=0;input.released=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
  assert(a.state.length==6);
  input={};if(first)paint();
  a.arena->paintText={};failAllocation=buildFailure;renderer.paintOk=buildFailure;paint();
  const auto bytes=std::string(reinterpret_cast<char*>(a.text.data.get()),a.state.length);
  for(auto b:{MappedInputManager::Button::Confirm,MappedInputManager::Button::NavNext}){
   input={};input.pressed=input.held=1u<<unsigned(b);input.heldTime=1001;a.loop();
   input.pressed=input.held=0;input.released=1u<<unsigned(b);a.loop();
  }
  input={};input.touchFlags=XP_UI_TOUCH_PRESSED|XP_UI_TOUCH_HELD;a.loop();
  input.touchFlags=XP_UI_TOUCH_RELEASED;a.loop();
  assert(a.tappedCursor==UINT32_MAX && !a.finished);
  assert(bytes==std::string(reinterpret_cast<char*>(a.text.data.get()),a.state.length));
  input={};failAllocation=false;renderer.paintOk=true;paint();a.loop();
  input.pressed=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
  input.pressed=0;input.released=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
  assert(a.state.length==bytes.size()+2);
  input={};renderer.paintOk=false;paint();
  input.released=1u<<unsigned(MappedInputManager::Button::Back);a.loop();
  assert(a.finished && a.result=="cancel");
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    out = Path(directory)
    for name in ['RustActivityAdapter.h', 'RustActivityAdapter.cpp']:
        (out / name).write_bytes((ROOT / 'src/activities' / name).read_bytes())
    stub = (ROOT / 'test/ui_runtime/stubs/Activity.h').read_text()
    stub = stub.replace('  SwipeDir swipe', '''  bool wasScreenTapped(int& x,int& y)const{x=10;y=10;return touchFlags&XP_UI_TOUCH_RELEASED;}
  SwipeDir swipe''')
    (out / 'Activity.h').write_text(stub)
    subprocess.run([sys.executable, str(ROOT/'test/keyboard_oracle/allocation_instrument.py'), str(ROOT), str(out/'keyboard-allocation.inc')], check=True)
    (out / 'keyboard.cpp').write_text(source)
    subprocess.run(['c++', '-std=c++20', '-pthread', '-UNDEBUG', '-I'+str(out),
                    '-I'+str(ROOT/'test/ui_runtime/stubs'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'),
                    '-Wno-inconsistent-missing-override', str(out/'keyboard.cpp'), str(out/'RustActivityAdapter.cpp'),
                    str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1], '-o', str(out/'keyboard')], check=True)
    subprocess.run([str(out/'keyboard')], check=True, timeout=30)
print('Production keyboard rejection, cursor, cancel and recovery paths pass')
