"""Freeze original reader projections, remote turns and end-book effects."""
from pathlib import Path
import gzip,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from extract import definition
sys.path.insert(0,str(ROOT/'test'));from rust_host import rust_link_flags
freeze='--freeze-original' in sys.argv
source=(subprocess.check_output(['git','show','8d534a99:src/activities/reader/EpubReaderActivity.cpp'],cwd=ROOT,text=True) if freeze else (ROOT/'src/activities/reader/EpubReaderActivity.cpp').read_text())
header=(subprocess.check_output(['git','show','8d534a99:src/activities/reader/EpubReaderActivity.h'],cwd=ROOT,text=True) if freeze else (ROOT/'src/activities/reader/EpubReaderActivity.h').read_text())
signatures=['ChapterPosition EpubReaderActivity::chapterPosition(','int EpubReaderActivity::bookPercentFor(','int EpubReaderActivity::currentTocIndex(','unsigned long EpubReaderActivity::confirmLongPressThreshold(','ScreenshotInfo EpubReaderActivity::getScreenshotInfo(','int EpubReaderActivity::getProgressBasisPoints(','CrossPointPosition EpubReaderActivity::getCurrentPosition(','bool EpubReaderActivity::remoteShortcut(','bool EpubReaderActivity::isAtEndOfBook(','void EpubReaderActivity::onReturnFromEndOfBook(','void EpubReaderActivity::onEndOfBookRendered(','bool EpubReaderActivity::pageTurn(','bool EpubReaderActivity::skipPages(']
methods='\n'.join(definition(source,s) for s in signatures)
for signature in ['bool EpubReaderActivity::applyPageMove(','uint32_t EpubReaderActivity::runProjection(']:
 if signature in source:methods+='\n'+definition(source,signature)
if 'bool EpubReaderActivity::inputOverPage(' in source:methods+='\n'+definition(source,'bool EpubReaderActivity::inputOverPage(')
else:methods+='\n'+definition(header,'bool inputOverPage()').replace('bool inputOverPage()','bool EpubReaderActivity::inputOverPage()').replace(' override','')
fixture=(HERE/'projection_oracle.cpp').read_text().replace('// PROJECTION_METHODS',methods)
if not freeze:
    fixture = fixture.replace('static int lockDepth', 'static bool projectionActive = false;\nstatic int lockDepth')
    fixture = fixture.replace('event("fallback-percent");', 'assert(!projectionActive); event("fallback-percent");')
    signature = 'uint32_t EpubReaderActivity::runProjection('
    if signature in fixture:
        start = fixture.index('{', fixture.index(signature)) + 1
        fixture = fixture[:start] + 'struct Active { Active(){assert(!projectionActive);projectionActive=true;} ~Active(){projectionActive=false;} } active;' + fixture[start:]

with tempfile.TemporaryDirectory() as directory:
 path=Path(directory)
 def run(text):
  (path/'projection.cpp').write_text(text)
  subprocess.run(['c++','-std=c++17','-UNDEBUG','-fsanitize=address,undefined','-I'+str(ROOT/'src'),'-I'+str(ROOT/'src/activities/reader'),'-I'+str(ROOT/'lib/KOReaderSync'),str(path/'projection.cpp'),*rust_link_flags(),'-o',str(path/'projection')],check=True)
  return subprocess.check_output([str(path/'projection')])
 actual=run(fixture);golden=HERE/'projection.golden.gz'
 if freeze:
  golden.write_bytes(gzip.compress(actual,mtime=0))
  for before,after in [('currentPage > 0 ?','currentPage > 1 ?'),('fraction, 0.0f, 1.0f','fraction, 0.0f, 0.5f'),('if (BoardConfig::hasTouch()) return 0;','if (false) return 0;')]:
   wrong=fixture.replace(before,after);assert wrong!=fixture and run(wrong)!=actual
 else:
  assert actual==gzip.decompress(golden.read_bytes())
  run((HERE/'projection_abi.cpp').read_text())
 print(f'projections: {len(actual.splitlines())} original trace lines')
