"""Freeze SDK keyboard placement, drawing commands, and hit rectangles."""
import pathlib
import subprocess
import tempfile
from record import ROOT, SDK, HERE, baseline


def record():
    recorder = (ROOT / 'test/ui_runtime/oracle.cpp').read_text().split('int main()')[0]
    # Include immutable application URL tables before replacing their owner.
    cpp = baseline('src/activities/util/KeyboardEntryActivity.cpp')
    tables = cpp[cpp.index('constexpr uint8_t URL_KEY_WIDTH'):cpp.index('}  // namespace')]
    main = r'''
int main() {
 for(int width:{480,528,792,800}) for(int layout=0;layout<13;layout++)
 for(int layer=0;layer<4;layer++) for(bool touch:{false,true}) {
  std::printf("case %d %d %d %d\n",width,layout,layer,touch);
  Recorder target;target.clip={0,0,static_cast<int16_t>(width),800};
  ui::DeviceContext device;device.width=width;device.height=800;
  ui::InteractionBuffer<56> hits; ui::Frame<56> frame(target,device,{},hits);
  ui::KeyboardProps props;
  props.layout=layout<10?&ui::builtinKeyboardLayout(static_cast<ui::KeyboardLayoutId>(layout),layer%2,layer>=2,true,true)
   :layout==10?&URL_LAYOUT:layout==11?&URL_SHIFT_LAYOUT:&URL_SNIPPET_LAYOUT;
  props.selectedIndex=touch?-1:0;props.keyAction=1;props.bottomHitOverflow=44;
  props.uniformKeyWidth=layout!=12;props.labelText.font=layout==9&&layer<2?0:1;
  if(!touch)props.background=ui::Paint::none();
  ui::keyboard(frame,{0,400,static_cast<int16_t>(width),static_cast<int16_t>(props.layout->rowCount*(touch?56:42)+(props.layout->rowCount-1)*6+8)},props);
  for(size_t i=0;i<hits.count();i++) {const auto& hit=hits.data()[i];Recorder::rect("hit",hit.rect);std::printf(" %u %d\n",hit.action,hit.value);}
 }
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp)
        (path / 'widgets.cpp').write_text(recorder + '\nnamespace fui=freeink::ui;\n' + tables + main)
        subprocess.run(['c++','-std=c++20','-O1','-DFREEINK_UI_COMPONENT_STYLE=0','-I'+str(SDK/'include'),
                        str(path/'widgets.cpp'),str(SDK/'src/FreeInkUI.cpp'),'-o',str(path/'widgets')],check=True)
        return subprocess.check_output([str(path/'widgets')])
