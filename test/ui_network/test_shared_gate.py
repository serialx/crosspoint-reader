"""Exercise production custom renderers and snapshot guards with fixed resource leaves."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("XP_REJECTION_SOURCE_ROOT", ROOT))


def method(family, name):
    source = (SOURCE / "src/activities/network" / (family + ".cpp")).read_text()
    marker = family + "::" + name + "("
    if marker not in source:
        return ""
    start = source.rfind("\n", 0, source.index(marker)) + 1
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


code = r'''
#include "RustActivityAdapter.h"
#include <xp_ui_network.h>
#include <cassert>
#include <cstdio>
#include <memory>
#include <optional>
struct Base:RustActivityAdapter {
 using RustActivityAdapter::RustActivityAdapter;
 using RustActivityAdapter::rustSceneCurrent;
 struct ResourceLock {template<class T>ResourceLock(T&){} void release(){}};
 bool buildOk=true; unsigned failures=0;
 bool rejected()const{return rustSceneRejected();}
 void reject(){rejectRustScene();}
 void publish(){publishRustScene({});}
 bool buildRustScene(XpUiScene& s)override{s.command_count=1;return buildOk;}
 void rustSceneFailed()override{++failures;}
};
struct WifiSelectionActivity:Base {
 using Base::Base;
 XpUiWifiState model{},renderingState{};XpUiListState next{};XpUiWifiHits hits{},nextHits{};
 bool routingReady=false,sceneFailed=false,suppressedTouch=false,sampledTouch=false;
 uint32_t suppressedButtons=0,sampledHeld=0;
 bool paintingRows=false,retireRequested=false;uint16_t rowCapacity=0;
 std::unique_ptr<int[]>visibleRows;
 std::optional<int>promptLabel,paintingIp,paintingSsid;
 Publication publication;Publication* rustPublication()override{return &publication;}
 void render(RenderLock&&)override;
 void rustSceneFailed()override;void rustPublished()override;
 // WIFI_CURRENT
};
struct CalibreConnectActivity:Base {
 using Base::Base;XpUiCalibreState model{},renderingState{};
 std::optional<int>paintingUploadLabel;
 void render(RenderLock&&)override;
 // CALIBRE_CURRENT
};
struct CrossPointWebServerActivity:Base {
 using Base::Base;XpUiWebState model{},renderingState{};
 std::optional<int>alternateUrl,primaryUrl,wifiConfig;
 void render(RenderLock&&)override;
 // WEB_CURRENT
};
struct NetworkModeSelectionActivity:Base {
 using Base::Base;XpUiListState state{},next{};
 // MODES_CURRENT
};
struct UsbDriveActivity:Base {
 using Base::Base;XpUiUsbState model{},renderingState{};
 // USB_CURRENT
};
'''

families = ["WifiSelectionActivity", "CalibreConnectActivity", "CrossPointWebServerActivity",
            "NetworkModeSelectionActivity", "UsbDriveActivity"]
for family, tag in zip(families, ["WIFI", "CALIBRE", "WEB", "MODES", "USB"]):
    current = method(family, "rustSceneCurrent")
    code = code.replace("// " + tag + "_CURRENT", "bool rustSceneCurrent()const override;" if current else "")
    code += current + "\n"
    if family in families[:3]:
        code += method(family, "render") + "\n"
    if family == families[0]:
        code += method(family, "rustSceneFailed").replace("{", "{++failures;", 1) + "\n"
        code += method(family, "rustPublished") + "\n"

code += r'''
template<class T>void failure(GfxRenderer& r,MappedInputManager& i){
 for(bool build:{false,true}){
  T a("network",r,i);a.buildOk=!build;r.paintOk=build;
  a.render(RenderLock{});assert(a.rejected()&&a.failures==1);
  a.buildOk=r.paintOk=true;a.render(RenderLock{});assert(!a.rejected());
 }
}
template<class T,class Change>void stale(T& a,MappedInputManager& i,Change change){
 a.publish();assert(!a.rejected());
 i.held=i.pressed=1u<<unsigned(MappedInputManager::Button::Confirm);a.loop();
 change();assert(!a.rustSceneCurrent());a.publish();assert(!a.rejected());
 a.reject();a.publish();assert(a.rejected());
}
int main(int argc,char**argv){
 assert(argc==2);GfxRenderer r;r.finishRefresh=true;MappedInputManager i;
 const std::string which=argv[1];
 if(which=="wifi-failure")failure<WifiSelectionActivity>(r,i);
 if(which=="calibre-failure")failure<CalibreConnectActivity>(r,i);
 if(which=="web-failure")failure<CrossPointWebServerActivity>(r,i);
 if(which=="wifi-stale"){
  for(int kind:{0,1,2}){WifiSelectionActivity a("wifi",r,i);
   stale(a,i,[&]{if(kind==0)++a.model.phase;else if(kind==1)++a.model.generation;else ++a.model.count;});}
 }
 if(which=="calibre-stale"){CalibreConnectActivity a("calibre",r,i);stale(a,i,[&]{++a.model.phase;});}
 if(which=="web-stale"){CrossPointWebServerActivity a("web",r,i);stale(a,i,[&]{++a.model.phase;});}
 if(which=="modes-stale"){NetworkModeSelectionActivity a("modes",r,i);stale(a,i,[&]{++a.state.navigation_revision;});}
 if(which=="usb-stale"){UsbDriveActivity a("usb",r,i);stale(a,i,[&]{++a.model.preparing;});}
 puts(argv[1]);
}
'''

with tempfile.TemporaryDirectory(prefix="network-gate-") as directory:
    out = Path(directory)
    for name in ["RustActivityAdapter.h", "RustActivityAdapter.cpp"]:
        (out / name).write_text((SOURCE / "src/activities" / name).read_text())
    stub = (ROOT / "test/ui_runtime/stubs/Activity.h").read_text().replace(
        "void displayBuffer(HalDisplay::RefreshMode)",
        "void displayBuffer(HalDisplay::RefreshMode = HalDisplay::FAST_REFRESH)")
    (out / "Activity.h").write_text(stub)
    (out / "check.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-pthread", "-I" + str(out),
                    "-I" + str(ROOT / "test/ui_runtime/stubs"),
                    "-I" + str(ROOT / "rust/crates/xp-ffi/include"),
                    str(out / "check.cpp"), str(out / "RustActivityAdapter.cpp"),
                    str(ROOT / "test/cmake/xp_ffi_host.cpp"), sys.argv[1], "-o", str(out / "check")], check=True)
    failed = []
    for case in sys.argv[2:]:
        result = subprocess.run([str(out / "check"), case])
        print(case, result.returncode, flush=True)
        if result.returncode:
            failed.append(case)
    if failed:
        raise SystemExit("Failed: " + ", ".join(failed))
