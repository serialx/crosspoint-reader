from pathlib import Path
import re,sys,subprocess,tempfile
repo=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(repo/'test/ui_network'))
import prepare
s=prepare.generate(frozen_usb=True,frozen_calibre=True,frozen_web=True)
a=re.search(r'struct CrossPointWebServerActivity\s*:\s*Activity\s*{',s).start();b=s.index('\n};',a)+3
cls='''struct CrossPointWebServerActivity:Activity {
 XpUiWebState model{};uint8_t cancelLatch=0;bool runningPolicy=false;
 struct PhaseRef {uint8_t& value;operator int()const{return value;}PhaseRef& operator=(WebServerActivityState s){value=static_cast<uint8_t>(s);return *this;}} state{model.phase};
 struct ModeRef {uint8_t& value;operator int()const{return value;}} networkMode{model.mode};
 uint8_t& isApMode=model.is_ap;uint8_t& startInJoinNetwork=model.start_in_join;uint8_t& leaveRequested=cancelLatch;
 uint32_t& lastHandleClientTime=model.last_handle;uint32_t& firstDisconnectAt=model.first_disconnect;
 int32_t& consecutiveDisconnects=model.disconnects;int32_t& lastWifiBars=model.bars;
 std::unique_ptr<CrossPointWebServer> webServer;std::string connectedIP,connectedSSID;
 struct ModelLock{explicit ModelLock(CrossPointWebServerActivity&) {++modelDepth;}~ModelLock(){--modelDepth;}};
 struct ResourceLock{explicit ResourceLock(CrossPointWebServerActivity&) {assert(modelDepth==0);}};
 struct CallContext;
 void run(uint8_t,uint8_t=0);static uint32_t service(void*,uint32_t,uint32_t);
 void onEnter();void onExit();void loop();void onNetworkModeSelected(NetworkMode);void onWifiSelectionComplete(bool);
};'''
s=s[:a]+cls+s[b:]
for method in prepare.METHODS['CrossPointWebServerActivity']:
 s=s.replace(prepare.extract(s,'CrossPointWebServerActivity',method),'')
code=(repo/'src/activities/network/CrossPointWebServerActivity.cpp').read_text()
a=code.index('struct CrossPointWebServerActivity::CallContext');b=code.index('\n};',a)+3
methods=re.search(r'constexpr uint8_t CALIBRE_RESULT = 5;',code).group()+'\n'+code[a:b]+'\n'+'\n'.join(prepare.extract(code,'CrossPointWebServerActivity',m) for m in ['onEnter','onExit','loop','onNetworkModeSelected','onWifiSelectionComplete','run','service'])
s=s.replace('static void start(',methods+'\nstatic void start(')
s=s.replace('static void record(const std::string& s) {','static int modelDepth=0;\nstatic void record(const std::string& s) {assert(modelDepth==0);')
s='#include "xp_ui_network.h"\n'+s
with tempfile.TemporaryDirectory() as directory:
 out=Path(directory);(out/'effects.cpp').write_text(s)
 subprocess.run(['c++','-std=c++20','-pthread','-Werror=switch','-I'+str(out),'-I'+str(repo/'rust/crates/xp-ffi/include'),str(out/'effects.cpp'),str(repo/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(out/'effects')],check=True)
 actual=subprocess.check_output([str(out/'effects')]);expected=(repo/'test/ui_network/effects.golden').read_bytes()
 if actual!=expected:
  import difflib
  sys.stderr.writelines(difflib.unified_diff(expected.decode().splitlines(True),actual.decode().splitlines(True)))
  raise SystemExit(1)
