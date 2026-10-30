"""Freeze original selection, retained-result and sync ownership/order traces."""
from pathlib import Path
import gzip,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from extract import definition
sys.path.insert(0,str(ROOT/'test'));from rust_host import rust_link_flags
freeze='--freeze-original' in sys.argv;release='--release-log' in sys.argv
reader=(subprocess.check_output(['git','show','8d534a99:src/activities/reader/EpubReaderActivity.cpp'],cwd=ROOT,text=True) if freeze else (ROOT/'src/activities/reader/EpubReaderActivity.cpp').read_text())
methods='\n'.join(definition(reader,s) for s in ['void EpubReaderActivity::openDictionaryWordSelect(', 'void EpubReaderActivity::startClipSelection(', 'void EpubReaderActivity::openFootnoteSelect(', 'void EpubReaderActivity::openClippings(', 'bool EpubReaderActivity::launchKOReaderSync('])
fixture=(HERE/'child_parent_oracle.cpp').read_text().replace('// CHILD_METHODS',methods)
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)
 for strip in [0,1]:
  def run(text):
   (p/'child.cpp').write_text(text)
   subprocess.run(['c++','-std=c++17','-UNDEBUG','-fsanitize=address,undefined',f'-DXP_STRIP_RENDER={strip}',*(['-DCHILD_RELEASE_LOG'] if release else []),str(p/'child.cpp'),*rust_link_flags(),'-o',str(p/'child')],check=True)
   return subprocess.check_output([str(p/'child')])
  actual=run(fixture);golden=HERE/f'child-parent-{strip}{"-release" if release else ""}.golden.gz'
  if freeze:
   golden.write_bytes(gzip.compress(actual,mtime=0))
   for a,b in [('endPage < section->pageCount','endPage <= section->pageCount'),('if (footnoteDepth > 0)','if (footnoteDepth > 1)'),('if (result.isCancelled) {','if (!result.isCancelled) {')]:
    wrong=fixture.replace(a,b);assert wrong!=fixture and run(wrong)!=actual,(a,strip,release)
  else:assert actual==gzip.decompress(golden.read_bytes())
  print(f'child parents strip={strip} release={release}: {len(actual.splitlines())} original trace lines',flush=True)
