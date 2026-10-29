import sys,pathlib,re,subprocess,gzip
root=pathlib.Path(__file__).resolve().parents[2]
work=pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0,str(root/'test/keyboard_oracle'))
import record
s=record.source(root)
s='#include "xp_ui_keyboard.h"\n'+s
s=s.replace('class KeyboardEntryActivity : public Activity {','class KeyboardEntryActivity : public Activity {\n public:\n XpUiKeyboardState state{};\n void prepare(); void adopt();')
fields={'length':'text.size()','cursor':'cursorPos','max_length':'maxLength','saved_cursor':'savedCursorPos','right_start_cursor':'rightStartCursorPos','hint_time':'hintShowTime','row':'selRow','col':'selCol','layout':'layoutId','input_type':'inputType','shifted':'shifted','symbols':'symbols','url_panel':'urlPanel','show_language':'showLangKey','cursor_mode':'cursorMode','toggle_position':'togglePos','password_visible':'passwordVisible','hint_visible':'hintVisible','delete_count':'delPressCount','confirm_held':'confirmHeld','confirm_long':'confirmLongHandled','up_held':'upHeld','up_long':'upLongHandled','down_held':'downHeld','down_long':'downLongHandled','right_held':'rightHeld','right_long':'rightLongHandled'}
extra='''
extern "C" {
XpUiKeyboardEffect oracle_activate(XpUiKeyboardState*,uint8_t*,uint32_t,const XpUiKeyboardLayouts*,int16_t,bool,uint32_t);
void oracle_mask(const XpUiKeyboardState*,const uint8_t*,uint8_t*,uint32_t);
void oracle_insert(XpUiKeyboardState*,uint8_t*,uint32_t,const char*);
bool oracle_backspace(XpUiKeyboardState*,uint8_t*,uint32_t);
bool oracle_sync(XpUiKeyboardState*,const XpUiKeyboardLayouts*,int16_t);
}
XpUiKeyboardLayouts layouts{
 nullptr,
 [](void*,uint8_t id,uint8_t shift,uint8_t symbols,uint8_t lang,uint8_t row)->XpUiKeyboardRow {
  const auto& l=freeink::ui::builtinKeyboardLayout(static_cast<freeink::ui::KeyboardLayoutId>(id),shift,symbols,true,lang);
  if(row>=l.rowCount)return {};const auto& r=l.rows[row];return {r.count,r.insetUnits,uint8_t(r.independentKeyWidth),0};
 },
 [](void*,uint8_t id,uint8_t shift,uint8_t symbols,uint8_t lang,uint8_t row,uint8_t col)->XpUiKeyboardKey {
  const auto& l=freeink::ui::builtinKeyboardLayout(static_cast<freeink::ui::KeyboardLayoutId>(id),shift,symbols,true,lang);
  if(row>=l.rowCount||col>=l.rows[row].count)return {};const auto& k=l.rows[row].keys[col];return {k.label,k.output,k.alt,k.value,uint8_t(k.kind),k.widthUnits};
 }};
'''
extra+='void KeyboardEntryActivity::prepare(){\n'
for field,var in fields.items(): extra+=f'state.{field}=static_cast<decltype(state.{field})>({var});\n'
extra+='state.enabled_layouts=keyboard_layouts::enabled();text.resize(4096);}\n'
extra+='void KeyboardEntryActivity::adopt(){\n'
for field,var in fields.items():
 if field=='length':extra+='text.resize(state.length);\n'
 else:extra+=f'{var}=static_cast<decltype({var})>(state.{field});\n'
extra+='}\n'

def replace(signature,new):
 global s
 start=s.index(signature);brace=s.index('{',start);depth=1;end=brace+1
 while depth: depth+=(s[end]=='{')-(s[end]=='}');end+=1
 s=s[:start]+new+s[end:]
replace('void KeyboardEntryActivity::insertUtf8(','''void KeyboardEntryActivity::insertUtf8(const char* out){prepare();oracle_insert(&state,reinterpret_cast<uint8_t*>(text.data()),4096,out);adopt();}''')
replace('bool KeyboardEntryActivity::backspaceUtf8(','''bool KeyboardEntryActivity::backspaceUtf8(){prepare();bool result=oracle_backspace(&state,reinterpret_cast<uint8_t*>(text.data()),4096);adopt();return result;}''')
replace('bool KeyboardEntryActivity::activateValue(','''bool KeyboardEntryActivity::activateValue(int16_t value,bool longPress){prepare();auto e=oracle_activate(&state,reinterpret_cast<uint8_t*>(text.data()),4096,&layouts,value,longPress,millis());adopt();if(e.navigation.kind==XP_UI_ACTIVATE)onComplete(text);return e.navigation.invalidate;}''')
replace('bool KeyboardEntryActivity::syncSelectionToValue(','''bool KeyboardEntryActivity::syncSelectionToValue(int16_t value){prepare();bool result=oracle_sync(&state,&layouts,value);adopt();return result;}''')
replace('std::string KeyboardEntryActivity::displayTextForCurrentState(','''std::string KeyboardEntryActivity::displayTextForCurrentState() const {auto s=state;s.length=text.size();s.cursor=cursorPos;s.input_type=uint8_t(inputType);s.cursor_mode=cursorMode;s.password_visible=passwordVisible;std::string out(text.size()+1,0);oracle_mask(&s,reinterpret_cast<const uint8_t*>(text.data()),reinterpret_cast<uint8_t*>(out.data()),out.size());out.resize(text.size());return out;}''')
replace('void KeyboardEntryActivity::loop()', '''void KeyboardEntryActivity::loop(){
 XpUiInput input{};auto& i=mappedInput;
 constexpr uint32_t masks[]={XP_UI_BACK,XP_UI_CONFIRM,XP_UI_LEFT,XP_UI_RIGHT,XP_UI_UP,XP_UI_DOWN,XP_UI_NAV_NEXT,XP_UI_NAV_PREVIOUS};
 for(unsigned b=0;b<8;b++){if(i.pressed&(1<<b))input.pressed|=masks[b];if(i.released&(1<<b))input.released|=masks[b];if(i.held&(1<<b))input.held|=masks[b];}
 input.now_ms=millis();input.held_ms=i.duration;
 prepare();auto e=xp_ui_keyboard_update(&state,reinterpret_cast<uint8_t*>(text.data()),4096,&layouts,&input,UINT32_MAX);adopt();
 if(e.navigation.invalidate)requestUpdate();if(e.navigation.kind==XP_UI_ACTIVATE)onComplete(text);if(e.navigation.kind==XP_UI_CANCEL)onCancel();
}''')
replace('uint16_t enabled() {', 'uint16_t enabled(){return xp_ui_keyboard_enabled(SETTINGS.keyboardLayouts,uint8_t(I18N.getLanguage()));}')
replace('freeink::ui::KeyboardLayoutId startingLayout() {', 'freeink::ui::KeyboardLayoutId startingLayout(){return static_cast<freeink::ui::KeyboardLayoutId>(xp_ui_keyboard_start(enabled(),uint8_t(I18N.getLanguage())));}')
replace('freeink::ui::KeyboardLayoutId next(const freeink::ui::KeyboardLayoutId current) {', 'freeink::ui::KeyboardLayoutId next(const freeink::ui::KeyboardLayoutId id){return static_cast<freeink::ui::KeyboardLayoutId>(xp_ui_keyboard_next(enabled(),uint8_t(id)));}')
# declarations must precede replacements; implementations need completed class.
pos=s.index('namespace fui = freeink::ui;')
s=s[:pos]+extra+s[pos:]
path=pathlib.Path(str(work/'diff.cpp'));path.write_text(s)
subprocess.run(['c++','-std=c++20','-O1','-DFREEINK_UI_COMPONENT_STYLE=0','-I'+str(record.SDK/'include'),'-I'+str(root/'lib/I18n'),'-I'+str(root/'rust/crates/xp-ffi/include'),str(path),str(record.SDK/'src/FreeInkUI.cpp'),str(work/'target/debug/libkeyboard_model_oracle.a'),'-o',str(work/'diff')],check=True)
b=subprocess.check_output([str(work/'diff')]);(work/'rust-transcript.txt').write_bytes(b)
a=b''.join(gzip.decompress(p.read_bytes())for p in sorted(record.HERE.glob('transcript-*.gz')))
if a!=b:
 for n,(x,y) in enumerate(zip(a.splitlines(),b.splitlines())):
  if x!=y:print(n,x,y);break
 print('FAIL',len(a),len(b));sys.exit(1)
print('PASS',len(a),'bytes')
