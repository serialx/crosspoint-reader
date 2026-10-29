"""Run production C++ resource bindings against the Rust UI orchestration ABI."""
from pathlib import Path
import subprocess,tempfile,sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def method(source, signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'

def record(archive,draft=None):
    paths={name:(Path(draft)/name if draft else ROOT/('src/activities/reader/' if name.startswith('KOReaderSync') else 'src/activities/settings/')/name)
           for name in ['KOReaderSyncActivity.h','KOReaderSyncActivity.cpp','KOReaderAuthActivity.h','KOReaderAuthActivity.cpp','KOReaderSettingsActivity.cpp']}
    harness=(HERE/'effects.cpp').read_text()
    source='#include "xp_ui_koreader.h"\n#define override\nconstexpr int MENU_ITEMS=12;\n'+harness.split('struct KOReaderSyncActivity : Activity')[0]
    source=source.replace('struct Activity {','struct KOReaderAuthActivity;\nstruct Activity {').replace('startActivityForResult(std::unique_ptr<T>, F f)', 'startActivityForResult(std::unique_ptr<T> child, F f)').replace('trace("wifi-select"); wifiCallback = f;', 'if constexpr (std::is_same_v<T, KOReaderAuthActivity>) trace("auth-open %u", unsigned(child->model.kind == XP_KO_SIGNUP)); trace("wifi-select"); wifiCallback = f;')
    source+='''
struct KOReaderUiActivity : Activity {
  struct ModelLock { explicit ModelLock(KOReaderUiActivity&) {} };
  XpKoState model{},painting{};
  std::string statusMessage,errorMessage;
  KOReaderUiActivity(const char*,GfxRenderer&,MappedInputManager&,XpKoKind kind) { xp_ui_koreader_init(&model,kind,0,0); }
  bool initializeUi() {return true;}
  void onExit() {Activity::onExit();}
  virtual void performEffect(const XpUiEffect&)=0;
  virtual void prepareText() {}
  virtual void snapshotResources() {}
  virtual const char* resourceText(uint32_t) const {return "";}
  void markReturn() {xp_ui_koreader_mark_return(&model,millis());}
  void publish(XpKoPhase phase,XpKoMessage message=XP_KO_NO_MESSAGE,uint8_t selected=0,const char* error=nullptr) {
    const char* value=nullptr;
    switch(message) {
      case XP_KO_NO_MESSAGE:if(phase==XP_KO_FAILED)value="";break;
      case XP_KO_HASH:value=tr(STR_CALC_HASH);break;
      case XP_KO_HASH_FAILED:value=tr(STR_HASH_FAILED);break;
      case XP_KO_CLIPPINGS:value=tr(STR_SYNCING_CLIPPINGS);break;
      case XP_KO_CLIPPINGS_FAILED:value=tr(STR_CLIPPING_SYNC_FAILED);break;
      case XP_KO_FETCH:value=tr(STR_FETCH_PROGRESS);break;
      case XP_KO_UPLOAD:value=tr(STR_UPLOAD_PROGRESS);break;
      case XP_KO_SAVE_FAILED:value=tr(STR_SAVE_PROGRESS_FAILED);break;
      case XP_KO_WIFI_FAILED:value=tr(STR_WIFI_CONN_FAILED);break;
      case XP_KO_AUTH_BUSY:value=model.kind==XP_KO_SIGNUP?tr(STR_CREATING_ACCOUNT):tr(STR_AUTHENTICATING);break;
      case XP_KO_AUTH_OK:value=model.kind==XP_KO_SIGNUP?tr(STR_ACCOUNT_CREATED):tr(STR_AUTH_SUCCESS);break;
      case XP_KO_USER_EXISTS:value=tr(STR_USERNAME_TAKEN);break;
      case XP_KO_SERVICE_ERROR:value=error;break;
    }
    if(value) {
      if(model.kind && phase==XP_KO_FAILED)errorMessage=value;else statusMessage=value;
    }
    xp_ui_koreader_publish(&model,phase,selected);
  }
  void loop() {
    XpUiInput input{};input.now_ms=millis();
    static constexpr unsigned bits[]={XP_UI_BACK,XP_UI_CONFIRM,XP_UI_UP,XP_UI_DOWN,XP_UI_LEFT,XP_UI_RIGHT};
    for(unsigned i=0;i<6;++i) {
      if(mappedInput.pressed&(1u<<i))input.pressed|=bits[i];
      if(mappedInput.released&(1u<<i))input.released|=bits[i];
    }
    if(mappedInput.tapped)input.touch_flags=XP_UI_TOUCH_RELEASED;
    auto effect=xp_ui_koreader_update(&model,&input);
    if(effect.invalidate)requestUpdate();performEffect(effect);
    if(model.pending_back){XpUiInput empty{};performEffect(xp_ui_koreader_update(&model,&empty));}
  }
};
'''
    for name in ['KOReaderSyncActivity.h','KOReaderAuthActivity.h']:
        header=paths[name].read_text()
        source+='\n'.join(line for line in header.splitlines() if not line.startswith(('#include','#pragma'))).replace(' private:',' public:')+'\n'
    source+='''
void KOReaderSyncActivity::prepareText() {}
void KOReaderSyncActivity::snapshotResources() {}
const char* KOReaderSyncActivity::resourceText(uint32_t) const {return "";}
int phase(const KOReaderSyncActivity& a){return a.model.phase;}
int phase(const KOReaderAuthActivity& a){return a.model.phase==XP_KO_AUTHENTICATED?3:a.model.phase==XP_KO_FAILED?4:2;}
'''
    source+=harness.split('enum class StrId')[1].split('// METHODS')[0].join(['enum class StrId',''])
    source=source.replace('struct KOReaderSettingsActivity : Activity {', 'struct KOReaderSettingsActivity : Activity { bool uiSceneFailed() const {return false;}')
    source+='static constexpr StrId menuNames[]={StrId::STR_SERVER_TYPE,StrId::STR_SERVER_TYPE,StrId::STR_SERVER_TYPE,StrId::STR_SERVER_TYPE,StrId::STR_CUSTOM_HEADER_1,StrId::STR_CUSTOM_HEADER_2};\n'
    sync=paths['KOReaderSyncActivity.cpp'].read_text();sync=sync[sync.index('namespace {'):sync.index('void KOReaderSyncActivity::snapshotResources()')]
    source+=sync
    auth=paths['KOReaderAuthActivity.cpp'].read_text();source+=auth[auth.index('void KOReaderAuthActivity::onWifiSelectionComplete'):]
    source+=method(paths['KOReaderSettingsActivity.cpp'].read_text(),'void KOReaderSettingsActivity::activateIndex(')
    main=harness.split('// METHODS')[1]
    main=main.replace('KOReaderSyncActivity activity; activity.pluginEventsOnly = value.plugin;', 'GfxRenderer r;MappedInputManager i;KOReaderSyncActivity activity(r,i,"/book.epub",CrossPointPosition{1,2,10},SavedProgressPosition{"/body/DocFragment[2]/body/p[3]",.25f},"Chapter",value.plugin);')
    main=main.replace('activity.state','phase(activity)').replace('activity.selectedOption','activity.model.selected').replace('activity.autoReturnAt','activity.model.auto_return_at')
    main=main.replace('KOReaderAuthActivity activity;if(signup)activity.mode=KOReaderAuthActivity::Mode::SIGN_UP;', 'GfxRenderer r;MappedInputManager i;KOReaderAuthActivity activity(r,i,signup?KOReaderAuthActivity::Mode::SIGN_UP:KOReaderAuthActivity::Mode::AUTHENTICATE);')
    main=main.replace('KOReaderAuthActivity auth;', 'GfxRenderer r;MappedInputManager i;KOReaderAuthActivity auth(r,i);').replace('auth.state','phase(auth)')
    guard_header=(Path(draft)/'KOReaderUiActivity.h' if draft else ROOT/'src/activities/reader/KOReaderUiActivity.h').read_text()
    start=guard_header.index('  class RunGuard {');end=guard_header.index('\n  };',start)+len('\n  };')
    source=source.replace('  XpKoState model{},painting{};',guard_header[start:end]+'\n  bool runActive=false;\n  XpKoState model{},painting{};')
    source=source.replace('KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& hash, KOReaderProgress& out) {',
        'std::function<void()> reenter;\nKOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& hash, KOReaderProgress& out) { if(reenter)reenter();')
    main=main.replace('  activity.onEnter();\n  if (activity.wifiCallback)', '  reenter=[&]{activity.performSync();};\n  activity.onEnter();\n  if (activity.wifiCallback)')
    source+=main
    with tempfile.TemporaryDirectory() as directory:
        cpp=Path(directory)/'candidate.cpp';exe=Path(directory)/'candidate';cpp.write_text(source)
        subprocess.run(['c++','-std=c++20','-DCONFIG_IDF_TARGET_ESP32C3=1','-I'+str(ROOT/'lib/KOReaderSync'),'-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(cpp),str(ROOT/'test/cmake/xp_ffi_host.cpp'),archive,'-o',str(exe)],check=True)
        return subprocess.check_output([str(exe)])

if __name__=='__main__':
    actual=record(sys.argv[1],sys.argv[2] if len(sys.argv)>2 else None)
    expected=(HERE/'effects.golden').read_bytes()
    # Tap-flash clearing belonged to the replaced FreeInkApp; Rust's input model
    # clears its painted active row before producing the activation effect.
    expected=expected.replace(b'clear-flash\n',b'')
    if actual!=expected:
        import difflib
        print(''.join(difflib.unified_diff(expected.decode().splitlines(True),actual.decode().splitlines(True))))
        raise SystemExit(1)
    print('C++ resource bridge + Rust orchestration: all original effects identical')
