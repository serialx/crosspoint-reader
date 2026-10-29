from pathlib import Path
import re,sys,subprocess,tempfile
repo=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(repo/'test/ui_network'))
import prepare
s=prepare.generate(frozen_usb=True, frozen_calibre=True, frozen_web=True)
a=re.search(r'struct CalibreConnectActivity\s*:\s*Activity\s*{',s).start();b=s.index('\n};',a)+3
cls='''struct CalibreConnectActivity:Activity {
 CalibreConnectActivity()=default;
 CalibreConnectActivity(GfxRenderer&,MappedInputManager&) {}
 XpUiCalibreState model{};
 std::unique_ptr<CrossPointWebServer> webServer;
 std::string connectedIP,connectedSSID,currentUploadName,lastCompleteName;
 uint32_t& lastHandleClientTime=model.last_handle;uint32_t& lastProgressReceived=model.received;uint32_t& lastProgressTotal=model.total;
 uint32_t& lastCompleteAt=model.completed_at;uint32_t& lastProcessedCompleteAt=model.processed_at;
 struct ModelLock{explicit ModelLock(CalibreConnectActivity&) {}};
 struct ResourceLock{explicit ResourceLock(CalibreConnectActivity&) {}};
 struct CallContext{CalibreConnectActivity& activity;std::optional<CrossPointWebServer::Status> upload;};
 void run(XpUiNetworkEvent,bool=false);static uint32_t service(void*,uint32_t,uint32_t);
 static void publish(void*,const XpUiCalibreState*,uint32_t);static void upload(void*,XpUiCalibreUpload*);
 void onEnter();void onExit();void loop();void onWifiSelectionComplete(bool);
 void startWebServer(){run(XP_UI_NETWORK_CHILD_RESULT,true);}
};'''
s=s[:a]+cls+s[b:]
for method in prepare.METHODS['CalibreConnectActivity']:
 s=s.replace(prepare.extract(s,'CalibreConnectActivity',method),'') if method!='startWebServer' else s
# Remove the original out-of-line start after retaining the fixture entry above.
s=s.replace(prepare.extract(s,'CalibreConnectActivity','startWebServer'),'')
code=(repo/'src/activities/network/CalibreConnectActivity.cpp').read_text()
methods='\n'.join(prepare.extract(code,'CalibreConnectActivity',m)for m in ['onEnter','onExit','loop','onWifiSelectionComplete','run','service','publish','upload'])
s=s.replace('static void start(',methods+'\nstatic void start(')
s='#include <optional>\n#include "xp_ui_network.h"\n'+s
with tempfile.TemporaryDirectory() as directory:
 out=Path(directory);(out/'effects.cpp').write_text(s)
 subprocess.run(['c++','-std=c++20','-pthread','-I'+str(out),'-I'+str(repo/'rust/crates/xp-ffi/include'),str(out/'effects.cpp'),str(repo/'test/cmake/xp_ffi_host.cpp'),sys.argv[1],'-o',str(out/'effects')],check=True)
 actual=subprocess.check_output([str(out/'effects')]);expected=(repo/'test/ui_network/effects.golden').read_bytes()
 if actual!=expected:
  import difflib
  sys.stderr.writelines(difflib.unified_diff(expected.decode().splitlines(True),actual.decode().splitlines(True)))
  raise SystemExit(1)
