import pathlib,subprocess,sys,gzip
root=pathlib.Path(__file__).resolve().parents[2]
work=pathlib.Path(sys.argv[1]).resolve()
sys.path.insert(0,str(root/'test/keyboard_oracle'))
import widgets,record
recorder=(root/'test/ui_runtime/oracle.cpp').read_text().split('int main()')[0]
cpp=record.baseline('src/activities/util/KeyboardEntryActivity.cpp')
tables=cpp[cpp.index('constexpr uint8_t URL_KEY_WIDTH'):cpp.index('}  // namespace')]
main=widgets.record.__code__.co_consts
main=next(x for x in main if isinstance(x,str) and 'int main() {' in x)
start=main.index('  ui::keyboard(frame,');end=main.index('  for(size_t',start)
main=main[:start]+r'''
  XpUiKeyboardState state{};state.layout=layout<10?layout:0;state.shifted=layer%2;state.symbols=layer>=2;state.show_language=1;
  state.input_type=layout>=10?2:0;state.url_panel=layout==12;state.active_key=INT16_MIN;
  if(layout>=10){state.shifted=layout==11;state.symbols=0;}
  XpUiKeyboardViewLayout style{};style.screen={0,0,int16_t(width),800};style.content=style.screen;style.font=1;style.small_font=0;style.key_spacing=6;style.touch=touch;
  XpUiCommand commands[80]{};XpUiInteraction rustHits[56]{};XpUiScene scene{};
  XpUiRect area{0,400,int16_t(width),int16_t(props.layout->rowCount*(touch?56:42)+(props.layout->rowCount-1)*6+8)};
  oracle_widget(&state,&layouts,&style,area,area.y+area.h+44,commands,rustHits,&scene);
  for(unsigned i=0;i<scene.command_count;i++){
   const auto& command=commands[i];const auto r=command.rect;
   if(command.kind==XP_UI_FILL){target.fill({r.x,r.y,r.w,r.h},ui::Paint::dither(ui::Color::LightGray),0,15);continue;}
   XpUiKeyboardPaintKey k{};unsigned index=command.resource-0x100;
   if(!xp_ui_keyboard_paint_key(&state,&layouts,index/16,index%16,&k))std::abort();
   ui::KeyboardKey key{k.label_present?k.label:nullptr,nullptr,ui::KeyKind(k.kind),ui::StateNormal,k.value,k.units,true,k.alternate_present?k.alternate:nullptr};
   ui::KeyboardRow row{&key,1,0};ui::KeyboardLayout one{&row,1};
   auto single=props;single.layout=&one;single.keyAction=0;single.selectedIndex=command.paint?0:-1;single.background=ui::Paint::none();single.uniformKeyWidth=false;
   ui::keyboard(frame,{int16_t(r.x-4),int16_t(r.y-4),int16_t(r.w+8),int16_t(r.h+8)},single);
  }
  for(unsigned i=0;i<scene.interaction_count;i++){const auto& h=rustHits[i];frame.hit({h.rect.x,h.rect.y,h.rect.w,h.rect.h},h.action,h.value,ui::InputTouch,ui::StateNormal);}
''' +main[end:]
callbacks=r'''
#include "xp_ui_keyboard.h"
extern "C" uint8_t oracle_widget(const XpUiKeyboardState*,const XpUiKeyboardLayouts*,const XpUiKeyboardViewLayout*,XpUiRect,int16_t,XpUiCommand*,XpUiInteraction*,XpUiScene*);
XpUiKeyboardLayouts layouts{nullptr,
 [](void*,uint8_t id,uint8_t sh,uint8_t sy,uint8_t la,uint8_t r)->XpUiKeyboardRow{const auto& l=ui::builtinKeyboardLayout(ui::KeyboardLayoutId(id),sh,sy,true,la);if(r>=l.rowCount)return {};const auto& x=l.rows[r];return {x.count,x.insetUnits,uint8_t(x.independentKeyWidth),0};},
 [](void*,uint8_t id,uint8_t sh,uint8_t sy,uint8_t la,uint8_t r,uint8_t c)->XpUiKeyboardKey{const auto& l=ui::builtinKeyboardLayout(ui::KeyboardLayoutId(id),sh,sy,true,la);if(r>=l.rowCount||c>=l.rows[r].count)return {};const auto& x=l.rows[r].keys[c];return {x.label,x.output,x.alt,x.value,uint8_t(x.kind),x.widthUnits};}};
'''
s=recorder+'\nnamespace fui=freeink::ui;\n'+tables+callbacks+main
p=pathlib.Path(str(work/'widget-diff.cpp'));p.write_text(s)
subprocess.run(['c++','-std=c++20','-O1','-DFREEINK_UI_COMPONENT_STYLE=0','-I'+str(record.SDK/'include'),'-I'+str(root/'rust/crates/xp-ffi/include'),str(p),str(record.SDK/'src/FreeInkUI.cpp'),str(work/'target/debug/libkeyboard_model_oracle.a'),'-o',str(work/'widget-diff')],check=True)
b=subprocess.check_output([str(work/'widget-diff')]);(work/'rust-widgets.txt').write_bytes(b)
a=gzip.decompress((record.HERE/'widgets.gz').read_bytes())
if a!=b:
 for n,(x,y)in enumerate(zip(a.splitlines(),b.splitlines())):
  if x!=y:print(n,x,y);break
 print('FAIL',len(a),len(b));sys.exit(1)
print('PASS',len(b),'bytes')
