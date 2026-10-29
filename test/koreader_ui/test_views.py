"""Frozen C++ placement, widget, text-origin and refresh transcripts."""
from pathlib import Path
import subprocess,tempfile,gzip,sys,re,json
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def record(mutate=False):
 source=(HERE/'view.cpp').read_text()
 methods=(HERE/'original_view.inc').read_text()
 if mutate:methods=methods.replace('top + 40','top + 41')
 source=source.replace('// METHODS',methods)
 translations={}
 for line in (ROOT/'lib/I18n/translations/english.yaml').read_text().splitlines():
  m=re.match(r'(STR_[A-Z0-9_]+): (".*")$',line)
  if m:translations[m[1]]=json.loads(m[2])
 used=set(re.findall(r'tr\((STR_[A-Z0-9_]+)\)',methods))
 source+='\nconst char* translation(const char* id) {\n'+''.join('if(std::string(id)=='+json.dumps(k)+')return '+json.dumps(translations[k])+';\n' for k in sorted(used))+'return id;}\n'
 with tempfile.TemporaryDirectory() as directory:
  cpp=Path(directory)/'view.cpp';exe=Path(directory)/'view'
  source=source.replace('../ui_settings/Recorder.h',str(ROOT/'test/ui_settings/Recorder.h'))
  cpp.write_text(source)
  sdk=ROOT/'freeink-sdk/libs/ui/FreeInkUI'
  subprocess.run(['c++','-std=c++20','-DFREEINK_UI_COMPONENT_STYLE=0','-I'+str(sdk/'include'),str(cpp),str(sdk/'src/FreeInkUI.cpp'),'-o',str(exe)],check=True)
  return subprocess.check_output([str(exe)])
if __name__=='__main__':
 result=record()
 if '--freeze' in sys.argv:(HERE/'views.golden.gz').write_bytes(gzip.compress(result,mtime=0))
 else:
  assert result==gzip.decompress((HERE/'views.golden.gz').read_bytes())
  assert record(True)!=result
  print('Original UI widget/placement/refresh golden and deliberate mutation: passed')
