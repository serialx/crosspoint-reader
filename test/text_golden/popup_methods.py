"""Extract production popup painters, metrics, and facts without copying policy."""
from pathlib import Path
import re
import sys

root, out = map(Path, sys.argv[1:])
sys.path.insert(0, str(root / 'test/reader_lifecycle'))
from test_popup_resource import definition, native_leaf
source = (root / 'src/components/themes/BaseTheme.cpp').read_text()
header = (root / 'src/components/themes/BaseTheme.h').read_text()
fields = re.findall(r'^  (?:int|float|bool) (popup\w+);', header, re.M)
parts = ['struct ThemeMetrics {']
for field in fields:
    parts.append(re.search(r'^  (?:int|float|bool) ' + field + ';', header, re.M).group())
parts.append('};')
for name, file in [('base', 'BaseTheme.h'), ('lyra', 'lyra/LyraTheme.h'),
                   ('rounded', 'roundedraff/RoundedRaffTheme.h')]:
    s = (root / 'src/components/themes' / file).read_text()
    values = [re.search(r'\.' + f + r'\s*=\s*([^,]+),', s).group(1) for f in fields]
    parts.append('static const ThemeMetrics '+name+'Metrics{'+','.join(values)+'};')
parts += ['struct Rect {int x,y,width,height;};',
          'struct UITheme { ThemeMetrics metrics=baseMetrics; static UITheme& getInstance(){static UITheme t;return t;} const ThemeMetrics& getMetrics() const {return metrics;} };',
          '#define GUI UITheme::getInstance()',
          'struct BaseTheme {Rect drawPopup(const GfxRenderer&,const char*)const; void fillPopupProgress(const GfxRenderer&,const Rect&,int)const;};',
          definition(source, 'Rect BaseTheme::drawPopup('),
          definition(source, 'void BaseTheme::fillPopupProgress('),
          'static auto readFacts = '+native_leaf((root/'src/activities/RustAppResources.cpp').read_text(),'popup_facts')+';']
out.write_text('\n'.join(parts)+'\n')
