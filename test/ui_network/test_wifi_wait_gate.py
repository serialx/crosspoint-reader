"""Reject a scene at an existing Wi-Fi lock wait, before a sampled action executes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import prepare_wifi_effects

ROOT = Path(__file__).resolve().parents[2]
source = prepare_wifi_effects.candidate()
source = source[:source.index("int main()")]
source = source.replace("static std::recursive_mutex panelMutex;", "static std::function<void()> waitHook;\nstatic std::recursive_mutex panelMutex;")
source = source.replace('record("lock");', 'record("lock"); if(waitHook)waitHook();')
source = source.replace("struct ResourceLock{explicit ResourceLock(WifiSelectionActivity&) {}};",
                        "struct ResourceLock{explicit ResourceLock(WifiSelectionActivity&) {if(waitHook)waitHook();}};")
if "bool rustSceneRejected()" not in source:
    source = source.replace("struct ModelLock{", "bool testRejected=false; bool rustSceneRejected()const{return testRejected;}\n struct ModelLock{", 1)
source += r'''
int main(int argc,char**argv){
 assert(argc==4);const std::string which=argv[1];const int target=atoi(argv[2]);const bool recovered=atoi(argv[3]);
 WifiSelectionActivity a;a.model.phase=which=="select"?XP_UI_WIFI_PHASE_LIST:
   which=="save"?XP_UI_WIFI_PHASE_SAVE:XP_UI_WIFI_PHASE_FORGET;
 a.model.count=1;a.model.list.selected=0;a.model.save_selection=0;a.model.forget_selection=1;
 a.selectedSSID="network";a.enteredPassword="secret";
 a.networks.push_back({"network",-45,true,true,false});a.networkStatuses.push_back("saved");
 int waits=0;
 waitHook=[&]{if(++waits==target){a.testRejected=!recovered;++a.model.list.navigation_revision;}};
 XpUiInput input{};if(which=="select")input.released=XP_UI_CONFIRM;else input.pressed=XP_UI_CONFIRM;
 a.run(XP_UI_WIFI_EVENT_TICK,0,&input);
 assert(waits>=target);
 for(const auto& event:transcript){
  assert(event.find("persist-")==std::string::npos);
  assert(event.find("finish")==std::string::npos);
  assert(event.find("keyboard")==std::string::npos);
  assert(event.find("scan-begin")==std::string::npos);
 }
 assert(a.model.phase==(which=="select"?XP_UI_WIFI_PHASE_LIST:
   which=="save"?XP_UI_WIFI_PHASE_SAVE:XP_UI_WIFI_PHASE_FORGET));
 puts(argv[1]);
}
'''
with tempfile.TemporaryDirectory(prefix="wifi-wait-") as directory:
    out = Path(directory)
    (out / "check.cpp").write_text(source)
    subprocess.run(["c++", "-std=c++20", "-pthread", "-I" + str(ROOT / "rust/crates/xp-ffi/include"),
                    str(out / "check.cpp"), str(ROOT / "test/cmake/xp_ffi_host.cpp"), sys.argv[1],
                    "-o", str(out / "check")], check=True)
    failed = []
    for case in ["select", "save", "forget"]:
        for target in ([1, 2, 3] if case == "select" else [2, 3]):
            for recovered in [0, 1]:
                args = [case, str(target), str(recovered)]
                result = subprocess.run([str(out / "check"), *args])
                print(args, result.returncode, flush=True)
                if result.returncode:
                    failed.append("/".join(args))
    if failed:
        raise SystemExit("Failed: " + ", ".join(failed))
