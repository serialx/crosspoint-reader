"""Compile the frozen C++ views with the real font and pixel target."""
from pathlib import Path
import re,json,sys
here=Path(__file__).resolve().parent;root=here.parents[1]
s=(here/'view.cpp').read_text();s=s[:s.index('// METHODS')]
s=s.replace('#include "../ui_settings/Recorder.h"','''#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_koreader.h>
#include <FreeInkUIIcon.h>
#include "components/icons/listIcons.h"
#include "Trace.h"
#include <array>
#include <cassert>
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui=freeink::ui;''')
s=re.sub(r'namespace EpdFontFamily\s*\{\s*enum Style\s*\{ REGULAR, BOLD \};\s*\}', '', s)
s=re.sub(r'constexpr int UI_10_FONT_ID\s*=\s*0;', '', s)
s=s[:s.index('struct GfxRenderer {')]+s[s.index('struct UITheme {'):]
s=s.replace('r.width','r.getScreenWidth()').replace('r.height','r.getScreenHeight()')
s=re.sub(r'Recorder::rect\("header",\s*r\);', '', s)
s=re.sub(r'printf\(" %s\\n",\s*title\);', '', s)
s=re.sub(r'printf\("hints \[%s\]\[%s\]\[%s\]\[%s\]\\n",\s*a,\s*b,\s*c,\s*d\);', '', s)
s=re.sub(r'constexpr int icon_download_24\s*=\s*0,\s*icon_upload_24\s*=\s*1;', '', s)
s=re.sub(r'namespace freeink::ui \{\s*BitmapRef bitmapFromIcon\(int\).*?\}\s*\}', '',s,flags=re.S)
s=s.replace('GfxRenderer renderer;', 'GfxRenderer& renderer;')
s=s.replace('struct KOReaderSyncActivity {', 'struct KOReaderSyncActivity {\n  explicit KOReaderSyncActivity(GfxRenderer& r):renderer(r) {}')
s=s.replace('struct KOReaderAuthActivity {', 'struct KOReaderAuthActivity {\n  explicit KOReaderAuthActivity(GfxRenderer& r):renderer(r) {}')
s=s.replace('renderer.width','renderer.getScreenWidth()').replace('renderer.height','renderer.getScreenHeight()')
s=re.sub(r'fui::Frame<64> frame\(renderer.target,\s*device,\s*\{\},\s*hits\);',
 'fui::GfxRendererTarget target(renderer,mappedInput.touch);target.setFont(0,UI_10_FONT_ID);target.setFont(1,UI_12_FONT_ID);\n'
 'fui::Frame<64> frame(target,device,{},hits);', s)
s=re.sub(r'for\s*\(size_t i\s*=\s*0;\s*i\s*<\s*hits.count\(\);\s*\+\+i\)\s*\{[^}]*\}', '', s)
s+='\n'+(here/'original_view.inc').read_text()+'\n'
translations={}
for line in (root/'lib/I18n/translations/english.yaml').read_text().splitlines():
 m=re.match(r'(STR_[A-Z0-9_]+): (".*")$',line)
 if m:translations[m[1]]=json.loads(m[2])
used=set(re.findall(r'tr\((STR_[A-Z0-9_]+)\)',s))
s+='\nconst char* translation(const char* id) {\n'+''.join('if(std::string(id)=='+json.dumps(k)+')return '+json.dumps(translations[k])+';\n' for k in sorted(used))+'return id;}\n'
s+=(here/'pixels_main.inc').read_text()
Path(sys.argv[1]).write_text(s)
