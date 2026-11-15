"""Compare additional batch/delete sequences using the unchanged frozen original."""
from pathlib import Path
import re,subprocess,tempfile,sys
from candidate_effects import run,filtered,HERE,ROOT
cases=(HERE/'effects_cases.inc').read_text().split('int main(){')[0]+r'''
int main(){
 GfxRenderer r;MappedInputManager i;
 for(bool update:{false,true}){
  reset(update?"multi-update-all":"multi-download-all");FontDownloadActivity a(r,i);seed(a);
  auto family=a.families_[0];a.families_.push_back(family);a.families_.push_back(family);a.filteredIndices_={0,1,2};
  for(auto&f:a.families_){f.installed=update;f.hasUpdate=update;}
  a.state_=FontDownloadActivity::FAMILY_LIST;a.nav.selected=0;a.activateSelected();
  for(const auto&f:a.families_)record("family installed=%d update=%d",f.installed,f.hasUpdate);
  record("batch state=%d files=%zu",a.state_,a.currentFileIndex_);
 }
 for(int result:{0,1}){
  reset("delete-result");FontDownloadActivity a(r,i);seed(a);a.families_[0].installed=true;
  a.state_=FontDownloadActivity::FAMILY_LIST;a.nav.selected=0;cfg.deleteResult=result;
  a.activateSelected();a.onDeleteConfirmationResult(ActivityResult{});
  record("delete state=%d installed=%d error=%s",a.state_,a.families_[0].installed,a.errorMessage_.c_str());
 }
}
'''
cases=re.sub(r'^static ActivityResult (?:pathResult|cancelled)\(\).*\n','',cases,flags=re.M)
original=(HERE/'original.inc').read_text();labels=sorted(set(re.findall(r'tr\((STR_\w+)\)',original)))
defines='\n'.join(f'#define {s} "{s}"'for s in labels)+'\n#define tr(x) x\n'
source=(HERE/'effects.cpp').read_text().replace('// LABELS',defines).replace('// ORIGINAL',original).replace('// CASES',cases)
source=source.replace('__DATE__','"Oct 29 2026"').replace('__TIME__','"00:00:00"')
with tempfile.TemporaryDirectory()as directory:
 cpp=Path(directory)/'original.cpp';exe=Path(directory)/'original';cpp.write_text(source)
 subprocess.run(['c++','-std=c++20',str(cpp),'-o',str(exe)],check=True)
 expected=filtered(subprocess.check_output([str(exe)],text=True))
run(ROOT/'src/activities/settings',Path(sys.argv[1]),cases,expected)
print('multi-family Download All/Update All and delete success/failure match frozen original')
