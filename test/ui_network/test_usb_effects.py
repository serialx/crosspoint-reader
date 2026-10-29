from pathlib import Path
import re
import sys
repo=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(repo/'test/ui_network'))
import prepare
root=repo/'src/activities/network'
s=prepare.generate(frozen_usb=True)
a=re.search(r'struct UsbDriveActivity\s*:\s*Activity\s*{',s).start();b=s.index('\n};',a)+3
cls='''struct UsbDriveActivity:Activity {
 XpUiUsbState model{};
 enum class PreparingFrame:uint8_t {Pending,Displayed,Rejected};
 PreparingFrame preparingFrame=PreparingFrame::Displayed;
 struct EnumRef {uint8_t& value;operator UsbDriveState()const{return static_cast<UsbDriveState>(value);}EnumRef& operator=(UsbDriveState s){value=static_cast<uint8_t>(s);return *this;}}state{model.phase};
 uint8_t& preparing=model.preparing;uint8_t& startFailed=model.start_failed;uint8_t& restartRequested=model.restart_requested;
 uint8_t& forcedDisconnectRequested=model.forced_disconnect_requested;uint8_t& hostSuspendPending=model.host_suspend_pending;
 uint32_t& hostWaitStartedAt=model.host_wait_at;uint32_t& startFailureStartedAt=model.start_failure_at;
 uint32_t& forcedDisconnectRequestedAt=model.forced_disconnect_at;uint32_t& hostSuspendStartedAt=model.host_suspend_at;
 struct ModelLock{explicit ModelLock(UsbDriveActivity&) {}};
 // Historical fixtures seed active USB phases without calling onEnter.
 UsbDriveActivity(){model.preparing=0;}
 bool preventAutoSleep(){return xp_ui_network_usb_prevent_sleep(&model);}
 void onEnter();void onExit();void loop();void run(XpUiNetworkEvent);static uint32_t service(void*,uint32_t,uint32_t);
};'''
s=s[:a]+cls+s[b:]
for method in prepare.METHODS['UsbDriveActivity']:
 s=s.replace(prepare.extract(s,'UsbDriveActivity',method),'')
code=(root/'UsbDriveActivity.cpp').read_text()
methods='\n'.join(prepare.extract(code,'UsbDriveActivity',m)for m in ['onEnter','onExit','loop','run','service'])
s=s.replace('static void start(',methods+'\nstatic void start(')
s='#include "xp_ui_network.h"\n'+s
import subprocess
import tempfile
archive = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    cpp = tmp/'effects.cpp'
    cpp.write_text(s)
    includes = repo/'rust/crates/xp-ffi/include'
    subprocess.run(['c++', '-std=c++20', '-pthread', '-I'+str(includes), str(cpp),
                    str(repo/'test/cmake/xp_ffi_host.cpp'), str(archive), '-o', str(tmp/'effects')], check=True)
    actual = subprocess.check_output([str(tmp/'effects')])
    expected = (repo/'test/ui_network/effects.golden').read_bytes().replace(b'reset-ui\n', b'')
    if actual != expected:
        import difflib
        sys.stderr.writelines(difflib.unified_diff(expected.decode().splitlines(True), actual.decode().splitlines(True)))
        raise SystemExit(1)
