"""Exercise actual bindings with SDK enum values deliberately permuted."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from layout_instrument import bindings, function

root = Path(__file__).resolve().parents[2]
sdk = root / 'freeink-sdk/libs/ui/FreeInkUI'
archive = Path(sys.argv[1])
painter = function((root / 'src/activities/util/KeyboardEntryActivity.cpp').read_text(), 'void KeyboardEntryActivity::paintKey(')
font = re.search(r'props.labelText.font =\s*(.*?);', painter, re.S)[1]
font = font.replace('arena->snapshot.', 's.').replace('fui::GfxRendererTarget::FONT_SMALL', '0').replace('fui::GfxRendererTarget::FONT_BODY', '1')
production = (root / 'src/activities/util/KeyboardLayoutSet.cpp').read_text()
source = bindings(root) + '\nint selectedFont(const XpUiKeyboardState& s) { return '+font+'; }\n' + '''
#include <cassert>
#include <cstring>
struct { uint16_t keyboardLayouts = 1023; } SETTINGS;
struct { Language language = Language::EN; Language getLanguage() const {return language;} } I18N;
'''+ re.sub(r'^#include[^\n]*\n', '', production, flags=re.M) + '''
int main() {
 using namespace keyboard_layouts;
 const auto callbacks = ProductionKeyboardLayouts::layouts();
 assert(xp_ui_keyboard_enabled(UINT16_MAX, 0) == (1u << COUNT) - 1);
 for(uint8_t i=0;i<COUNT;++i) {
  I18N.language=ALL[i].language;
  XpUiKeyboardState state{};state.layout=i;
  assert(selectedFont(state)==(ALL[i].id==fui::KeyboardLayoutId::ArabicAr?0:1));
  state.symbols=1;assert(selectedFont(state)==1);
  assert(xp_ui_keyboard_start(1023,uint8_t(I18N.language)) == i);
  assert(startingLayout() == ALL[i].id);
  for(uint16_t mask=1;mask<1024;++mask) {
   SETTINGS.keyboardLayouts=mask;
   const auto enabledMask=enabled();
   assert(enabledMask == (mask & LATIN_BITS ? mask : mask|1));
   const auto nextIndex=xp_ui_keyboard_next(enabledMask,i);
   assert(next(ALL[i].id)==ALL[nextIndex].id);
   assert(startingLayout()==ALL[xp_ui_keyboard_start(enabledMask,uint8_t(I18N.language))].id);
  }
  SETTINGS.keyboardLayouts=1023;
  for(uint8_t shift=0;shift<2;++shift) for(uint8_t symbols=0;symbols<2;++symbols) {
   const auto& expected=fui::builtinKeyboardLayout(ALL[i].id,shift,symbols,true,true);
   for(uint8_t r=0;r<expected.rowCount;++r) {
    const auto row=callbacks.row(nullptr,i,shift,symbols,1,r);
    assert(row.count==expected.rows[r].count);
    for(uint8_t c=0;c<row.count;++c) {
     const auto actual=callbacks.key(nullptr,i,shift,symbols,1,r,c);
     const auto& key=expected.rows[r].keys[c];
     assert(actual.label==key.label && actual.output==key.output && actual.alternate==key.alt);
     assert(actual.value==key.value && actual.kind==uint8_t(key.kind) && actual.units==key.widthUnits);
    }
   }
  }
 }
 assert(next(static_cast<fui::KeyboardLayoutId>(255))==ALL[1].id);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    work = Path(tmp)
    shutil.copytree(sdk / 'include', work / 'include')
    enum = work / 'include/components/keyboard/keyboard.h'
    original = enum.read_text()
    body = re.search(r'enum class KeyboardLayoutId : uint8_t \{(.*?)\};', original, re.S)[1]
    names = re.findall(r'^\s*(\w+)\s*(?:=\s*\d+)?\s*,?', body, re.M)
    assert len(names) == 10, names
    vendor = (sdk / 'src/FreeInkUI.cpp').read_text().replace('builtinKeyboardLayout(', 'sourceBuiltinKeyboardLayout(')
    (work/'vendor.cpp').write_text(vendor)
    subprocess.run(['c++','-std=c++20','-O1','-I'+str(sdk/'include'),'-c',str(work/'vendor.cpp'),'-o',str(work/'vendor.o')],check=True)
    shim = '''
namespace freeink::ui {
const KeyboardLayout& sourceBuiltinKeyboardLayout(KeyboardLayoutId,bool,bool,bool,bool);
const KeyboardLayout& builtinKeyboardLayout(KeyboardLayoutId id,bool sh,bool sy,bool controls,bool language) {
 const KeyboardLayoutId ids[]{'''+','.join('KeyboardLayoutId::'+n for n in names)+'''};
 for(uint8_t i=0;i<10;++i) if(ids[i]==id)
  return sourceBuiltinKeyboardLayout(static_cast<KeyboardLayoutId>(i),sh,sy,controls,language);
 assert(false);return sourceBuiltinKeyboardLayout(KeyboardLayoutId(0),sh,sy,controls,language);
}
}
'''
    # Non-contiguous values ensure the test cannot accidentally rely on ordinals.
    permutation = [17, 3, 29, 11, 5, 23, 7, 31, 13, 19]
    changed = ',\n'.join(f'  {name} = {value}' for name, value in zip(names, permutation))
    for label, text in [('pinned', original), ('permuted', original.replace(body, '\n'+changed+'\n'))]:
        enum.write_text(text)
        (work / 'mapping.cpp').write_text(source + shim)
        subprocess.run(['c++','-std=c++20','-O1','-DFREEINK_UI_COMPONENT_STYLE=0',
                        '-I'+str(work/'include'),'-I'+str(root/'lib/I18n'),
                        '-I'+str(root/'rust/crates/xp-ffi/include'),str(work/'mapping.cpp'),
                        str(work/'vendor.o'),str(root/'test/cmake/xp_ffi_host.cpp'),str(archive),'-o',str(work/'mapping')],check=True)
        subprocess.run([str(work/'mapping')],check=True)
        print(label, 'SDK IDs: every callback, language/count contract and start/next round trip passed')
