"""Interleave real update input/publication/effect methods with service leaves."""
from pathlib import Path
import argparse
from candidate_effects import run, HERE, ROOT
from test_failure_bridge import method

parser = argparse.ArgumentParser()
parser.add_argument('library', type=Path)
parser.add_argument('--source', type=Path, default=ROOT/'src/activities/settings')
args = parser.parse_args()
bridge = (args.source/'UpdateUiActivity.cpp').read_text()
host = (HERE/'candidate_host.inc').read_text()
host = host.replace('struct UpdateUiActivity:Activity {', '''
namespace haptic_feedback { void touchAction(bool) {} }
struct UpdateUiActivity:Activity {
 struct Publication { bool ready = true; } publication;
 XpUiUpdateFeedback next{};
 XpUiUpdateEffect pending{};
 bool pendingHaptic = false, routesReady = true;
 int inputRowCount = 4, inputFamilyCount = 3;
 XpUiEffect updateRust(const XpUiInput&);
 void applyRustEffect(const XpUiEffect&);
 void rustPublished();
''')
host += '\n' + '\n'.join(method(bridge, 'UpdateUiActivity::'+name)
                          for name in ['updateRust', 'rustPublished', 'applyRustEffect'])
cases = (HERE/'effects_cases.inc').read_text().split('int main(){')[0] + r'''
int main(){
 GfxRenderer r; MappedInputManager i;
 for(bool installed:{false,true}) {
  reset(installed?"delete-tapped-family":"download-tapped-family");
  FontDownloadActivity a(r,i); seed(a);
  const char names[]="\0First\0Tapped\0Last\0one.cpfont\0two.cpfont\0";
  a.stringArena_=std::make_unique<char[]>(sizeof(names));
  memcpy(a.stringArena_.get(),names,sizeof(names));
  auto family=a.families_[0];
  a.families_={family,family,family};
  a.families_[0].name=1; a.families_[1].name=7; a.families_[2].name=14;
  a.families_[1].installed=installed;
  a.files_[0].name=19; a.files_[1].name=30;
  a.filteredIndices_={0,1,2};
  a.model.phase=FontDownloadActivity::FAMILY_LIST;
  assert(a.isDownloadAllRow(0));
  a.painting=a.model;
  a.next.rows.first_row={0,0,100,20}; a.next.rows.count=4; a.next.rows.uniform=1;
  a.next.list=a.model.list;
  a.rustPublished();
  XpUiInput input{}; input.touch_flags=XP_UI_TOUCH_RELEASED; input.touch_x=10; input.touch_y=50;
  auto effect=a.updateRust(input);
  assert(a.model.list.selected==2);
  // Pause the main loop here; the render task publishes its older frame.
  a.rustPublished();
  a.applyRustEffect(effect);
  record("installed %d %d %d",a.families_[0].installed,a.families_[1].installed,a.families_[2].installed);
 }
}
'''
# Unused original-only helpers need no definitions in this fixture.
cases = '\n'.join(line for line in cases.splitlines()
                  if not line.startswith('static ActivityResult '))
actual = run(args.source, args.library, cases, expected=False, host=host)
print(actual)
download, deletion = actual.split('CASE delete-tapped-family\n')
download_ok = 'installed 0 1 0\n' in download
delete_ok = 'confirm STR_DELETE | Tapped\n' in deletion
print(f'individual download: {download_ok}; tapped delete prompt: {delete_ok}')
assert download_ok and delete_ok, 'stale publication changed the action targets'
assert 'installed 0 1 0\n' in deletion
print('touch/publication interleaving preserves individual download and delete targets')
