"""Combine production Wi-Fi effects, renderer, and shared gate with host I/O leaves."""
from pathlib import Path
import runpy
import sys
import tempfile
import prepare
import prepare_wifi_extra

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
output = Path(sys.argv[1])
with tempfile.TemporaryDirectory() as tmp:
    generated = Path(tmp) / 'facade.cpp'
    sys.argv = [str(HERE / 'prepare_wifi_facade_target.py'), str(generated), 'legacy']
    runpy.run_path(sys.argv[0], run_name='__main__')
    source = generated.read_text()
source = source[:source.index('int main() {')]
start = source.index('void checkWifiFailureAndPanel(')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
source = source[:start] + source[end:]
source = source.replace(prepare.extract(source, 'Candidate', 'service'), '')
leaves = prepare_wifi_extra.source()
logging = leaves[leaves.index('static std::vector<std::string> transcript;'):leaves.index('struct MappedInputManager')]
logging = logging[:logging.index('size_t strlcpy(')]
logging = logging.replace('uint32_t millis() { return nowMs; }', '')
logging = logging.replace('#define LOG_ERR(tag, ...) log("ERR", tag, __VA_ARGS__)', '')
network = leaves[leaves.index('using wl_status_t'):leaves.index('#define tr(x)')]
network = network.replace('struct WifiCredential {\n  std::string ssid, password;\n};', '')
network = 'std::function<void()> scanDeleteHook;\n' + network
network = network.replace('void scanDelete() { record("scan-delete"); }',
                          'void scanDelete() { record("scan-delete"); scan=-2; rows.clear(); if(scanDeleteHook)scanDeleteHook(); }')
# These are hardware/storage leaves from the existing effect oracle, not a model substitute.
position = source.index('struct FakeBase {')
extra = '''
#include <variant>
struct KeyboardResult {std::string text;};
struct WifiResult {bool connected;std::string ssid,ip;};
struct ActivityResult {bool isCancelled=false;std::variant<WifiResult,KeyboardResult> data;};
std::function<void(const ActivityResult&)> childResult;
enum class InputType {Text};
struct KeyboardEntryActivity {
 KeyboardEntryActivity(GfxRenderer&,MappedInputManager&,const char* label,const char*,int,InputType){record(std::string("keyboard:")+label);}
};
namespace haptic_feedback {void touchAction(bool){}}
'''
# Logging requires cstdarg before its first use.
source = source[:position] + '#include <cstdarg>\n' + logging + network + extra + source[position:]
source = source.replace('struct RenderLock {};', 'struct RenderLock {RenderLock()=default;template<class T>explicit RenderLock(T&) {assert(allocations::criticalDepth==0);}};')
source = source.replace('struct FakeBase {', '''struct FakeBase {
 template<class T,class F>void startActivityForResult(std::unique_ptr<T>,F f){childResult=f;}
 bool finished=false;
 void setResult(ActivityResult r){record(r.isCancelled?"result:cancelled":"result:connected");}
 void finish(){finished=true;record("finish");}
''')
source = source.replace('void xSemaphoreGive(SemaphoreHandle_t s){s->mutex.unlock();}', '''std::function<void()> unlockHook;
void xSemaphoreGive(SemaphoreHandle_t s){s->mutex.unlock();if(unlockHook)unlockHook();}''')
production = (ROOT / 'src/activities/network/WifiSelectionActivity.cpp').read_text()
methods = '\n'.join(prepare.extract(production, 'WifiSelectionActivity', name) for name in [
    'service', 'keyboard', 'appendHiddenNetworkEntry', 'rebuildNetworkStatuses', 'getSignalStrengthIndicator', 'onComplete'
]).replace('WifiSelectionActivity', 'Candidate')
source = source.replace('struct Candidate final', 'struct Candidate;\nstd::function<void(Candidate&)> publishHook;\nstruct Candidate final')
publish = prepare.extract(source, 'Candidate', 'publish')
source = source.replace(publish, publish[:-1] + 'if(publishHook)publishHook(self);\n}')
source += methods + '\n' + (HERE / 'wifi_partial_main.inc').read_text()
output.write_text(source)
