"""Freeze reader clipping traversal, word geometry, hit testing and plane painting."""
from pathlib import Path
import gzip
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from extract import definition
sys.path.insert(0, str(ROOT / 'test'))
from rust_host import rust_link_flags
freeze = '--freeze-original' in sys.argv
release = '--release-log' in sys.argv
def source(path):
    return (subprocess.check_output(['git','show','8d534a99:'+path],cwd=ROOT,text=True) if freeze else (ROOT/path).read_text())
reader=source('src/activities/reader/EpubReaderActivity.cpp')
clipping=source('src/ClippingStore.h')
methods='\n'.join(definition(reader,s) for s in ['int EpubReaderActivity::clippingAtPoint(', 'void EpubReaderActivity::drawClippingHighlights('])
geometry='\n'.join(definition(reader,s) for s in ['int clippingWordFont(', 'Rect clippingWordRect('] if s in reader)
if 'int EpubReaderActivity::runClippingPolicy(' in reader:
    methods += '\n' + definition(reader,'int EpubReaderActivity::runClippingPolicy(')
match='\n'.join(definition(clipping,s) for s in ['inline bool clippingStoredRangeMatchesLayout(', 'inline bool clippingContainsWord('])
fixture=(HERE/'clipping_parent_oracle.cpp').read_text().replace('// CLIPPING_RECORD',next(line for line in clipping.splitlines() if line.startswith('inline constexpr size_t CLIPPING_CHAPTER_TITLE_MAX'))+'\n'+definition(clipping,'struct Clipping {')+';').replace('// CLIPPING_MATCH',match).replace('// GEOMETRY_METHODS',geometry).replace('// CLIPPING_METHODS',methods)
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    def run(text):
        (path/'clipping.cpp').write_text(text)
        subprocess.run(['c++','-std=c++17','-UNDEBUG','-fsanitize=address,undefined','-I'+str(ROOT/'src'),
                        *(['-DCLIPPING_RELEASE_LOG'] if release else []),str(path/'clipping.cpp'),*rust_link_flags(),'-o',str(path/'clipping')],check=True)
        return subprocess.check_output([str(path/'clipping')])
    actual=run(fixture)
    golden=HERE/f'clipping-parent{"-release" if release else ""}.golden.gz'
    if freeze:
        golden.write_bytes(gzip.compress(actual,mtime=0))
        for before,after in [('if (width <= 0) continue;','if (width < 0) continue;'),
                             ('x + (x & 1)','x'),
                             ('sourceEnd > clipping.startOffset','sourceEnd >= clipping.startOffset')]:
            wrong=fixture.replace(before,after)
            assert wrong!=fixture and run(wrong)!=actual,(before,release)
    else:assert actual==gzip.decompress(golden.read_bytes())
    print(f'clipping parents release={release}: {len(actual.splitlines())} original trace lines')
