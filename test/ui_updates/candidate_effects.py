"""Compare current service bindings + real Rust policy with the frozen C++ effects."""
from pathlib import Path
import argparse
import difflib
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
NAMES = ['OtaUpdate', 'SdFirmwareUpdate', 'ClearCache', 'FontDownload']
EXCLUDE = {'fetchAndParseManifest', 'internString', 'computeFileCrc32', 'rebuildRowItems', 'rebuildGroupRowItems', 'rebuildFamilyRowItems', 'prepareRows', 'row'}

def filtered(data):
    return '\n'.join(line.rstrip() for line in data.splitlines() if not line.startswith(('paint.', 'popup', 'DBG UI ', 'DBG OTA Update progress:', 'tap.clear', 'routing.close')))+'\n'

def run(source, library):
    headers, methods = [], []
    for name in NAMES:
        h = (source / f'{name}Activity.h').read_text()
        if name == 'FontDownload':
            a, b = h.index('// JSON schema'), h.index('class FontDownloadActivity')
            h = h[:a] + '#define FONT_MANIFEST_URL "manifest"\n' + h[b:]
        h = re.sub(r'^#(?:include|pragma).*\n', '', h, flags=re.M).replace(' private:', ' public:')
        # Class-default private fields are also inspected by the fixtures.
        h = h.replace('public UpdateUiActivity {', 'public UpdateUiActivity {\n public:')
        headers.append(h)
        cpp = (source / f'{name}Activity.cpp').read_text()
        starts = list(re.finditer(r'^(?:[\w:<>&*]+(?:\s+[\w:<>&*]+)*\s+)?'+name+r'Activity::[\w~]+\(', cpp, re.M))
        for i, match in enumerate(starts):
            text = cpp[match.start():starts[i+1].start() if i+1<len(starts) else len(cpp)]
            method = re.search(name+r'Activity::(\w+)\(', text)[1]
            if method not in EXCLUDE: methods.append(text)
    bodies = '\n'.join(headers+methods)
    labels = sorted(set(re.findall(r'tr\((STR_\w+)\)', bodies)))
    defines = '\n'.join(f'#define {s} "{s}"' for s in labels)+'\n#define tr(x) x\n'
    cases = (HERE/'effects_cases.inc').read_text()
    cases = re.sub(r'fui::ListNav& FontDownloadActivity::activeNav\(\).*?\n', '', cases)
    cases = cases.replace('void FontDownloadActivity::buildScreen(UiScreen&){}','void FontDownloadActivity::prepareRows(){}\nbool FontDownloadActivity::row(uint16_t,XpUiSettingsRow&)const{return false;}')
    for old,new in [('state_','model.phase'),('state','model.phase'),('currentFileTotal_','model.file_count'),('currentFileIndex_','model.file_index'),('cancelRequested_','model.cancel_requested'),('goHomeRequested_','model.home_requested'),('fileProgress_','model.processed'),('fileTotal_','model.total'),('lastUpdaterPercentage','model.last_percent'),('lastRenderedPercent','model.last_percent'),('writtenBytes','model.processed'),('confirmPopup.selected','model.popup_selected')]:
        cases=cases.replace('a.'+old,'a.'+new).replace('c.'+old,'c.'+new)
    cases=cases.replace('a.handleCustomInput()','a.loop()').replace('int(c.state)','int(c.model.phase)')
    cases=cases.replace('OtaUpdateActivity::UPDATE_IN_PROGRESS','3').replace('OtaUpdateActivity::State(state)','state').replace('SdFirmwareUpdateActivity::State::FAILED','5').replace('SdFirmwareUpdateActivity::State::UPDATING','3')
    cases=cases.replace('a.updater.processed=n;','a.updater.processed=n;a.notify(XP_UI_UPDATE_PROGRESS,n,a.updater.total);').replace('a.model.processed=n;','a.notify(XP_UI_UPDATE_PROGRESS,n,a.firmwareSize);')
    cases=cases.replace('a.model.phase=OtaUpdateActivity::State(state)','a.model.phase=state')
    cases=cases.replace('a.model.phase_=','a.model.phase=').replace('a.model.phase_','a.model.phase')
    # Fixed-width file index matches the original size_t diagnostic on both hosts.
    cases=cases.replace('a.model.file_index,','size_t(a.model.file_index),')
    text=(HERE/'effects.cpp').read_text().replace('// LABELS',defines).replace('// ORIGINAL',(HERE/'candidate_host.inc').read_text()+'\n'+bodies).replace('// CASES',cases)
    text=text.replace('__DATE__','"Oct 29 2026"').replace('__TIME__','"00:00:00"')
    with tempfile.TemporaryDirectory() as directory:
        cpp,exe=Path(directory)/'candidate.cpp',Path(directory)/'candidate'
        cpp.write_text(text)
        subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Wno-unused-parameter','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(cpp),str(ROOT/'test/cmake/xp_ffi_host.cpp'),str(library),'-o',str(exe)],check=True)
        actual=filtered(subprocess.check_output([str(exe)],text=True))
    expected=filtered((HERE/'effects.golden').read_text())
    if expected!=actual:
        print(''.join(difflib.unified_diff(expected.splitlines(True),actual.splitlines(True),fromfile='original',tofile='candidate')))
        raise SystemExit(1)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('library',type=Path)
    parser.add_argument('--source',type=Path,default=ROOT/'src/activities/settings')
    args=parser.parse_args()
    run(args.source,args.library)
