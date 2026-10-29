"""Exercise actual resource-lock/header and render/publish methods under ASan."""
from pathlib import Path
import shutil, subprocess, sys, tempfile, re
import prepare
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
header=(ROOT/'src/activities/network/CalibreConnectActivity.h').read_text()
header='\n'.join(line for line in header.splitlines() if not line.startswith('#'))
header=header.replace('class CalibreConnectActivity final','struct CalibreConnectActivity final')
source=(HERE/'calibre_input.cpp').read_text().replace('// PRODUCTION_CLASS',header)
production=(ROOT/'src/activities/network/CalibreConnectActivity.cpp').read_text()
methods=['onEnter','onExit','onWifiSelectionComplete','loop','run','render','publish','upload']
source=source.replace('// PRODUCTION_METHODS','\n'.join(prepare.extract(production,'CalibreConnectActivity',m) for m in methods))
with tempfile.TemporaryDirectory() as directory:
    out=Path(directory)
    for name in ['RustActivityAdapter.h','RustActivityAdapter.cpp']:
        shutil.copyfile(ROOT/'src/activities'/name,out/name)
    stub=(ROOT/'test/ui_runtime/stubs/Activity.h').read_text()
    stub=stub.replace('using portMUX_TYPE', 'inline thread_local int modelDepth=0;\nusing portMUX_TYPE')
    stub=stub.replace('#define taskENTER_CRITICAL(m) (m)->lock()', '#define taskENTER_CRITICAL(m) do {(m)->lock(); ++modelDepth;} while(0)')
    stub=stub.replace('#define taskEXIT_CRITICAL(m) (m)->unlock()', '#define taskEXIT_CRITICAL(m) do {--modelDepth; (m)->unlock();} while(0)')
    stub=stub.replace('bool drawUiScene(const XpUiScene&, const XpUiPaint*) const { return paintOk; }', '''bool acceptScene=true;
  bool drawUiScene(const XpUiScene&,const XpUiPaint* paint) const {
    if(!acceptScene) return false;
    if(paint->resource) paint->resource(paint->context,nullptr,nullptr,{});
    return true;
  }''')
    stub=stub.replace('  virtual void loop() {}','  virtual void onEnter() {}\n  virtual void onExit() {}\n  virtual bool skipLoopDelay(){return false;}\n  virtual bool preventAutoSleep(){return false;}\n  virtual void loop() {}')
    stub=stub.replace('void displayBuffer(HalDisplay::RefreshMode)', 'void displayBuffer(HalDisplay::RefreshMode = HalDisplay::FAST_REFRESH)')
    (out/'Activity.h').write_text(stub)
    (out/'input.cpp').write_text(source)
    subprocess.run(['c++','-std=c++20','-pthread','-fsanitize=address','-fno-omit-frame-pointer',
        '-I'+str(out),'-I'+str(ROOT/'test/ui_runtime/stubs'),'-I'+str(ROOT/'rust/crates/xp-ffi/include'),
        str(out/'input.cpp'),str(out/'RustActivityAdapter.cpp'),str(ROOT/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(out/'input')],check=True)
    subprocess.run([str(out/'input')],check=True)
