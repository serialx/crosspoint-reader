"""Exercise production transfer-pump and arena failure paths with a busy panel."""
import pathlib
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]

def method(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class Adapter(unittest.TestCase):
    def test_progress_polls_before_panel_finishes(self):
        path = ROOT/'src/activities/CatalogActivity.cpp'
        if 'onDownloadProgress' not in path.read_text() or 'RustActivityAdapter::loop' not in path.read_text():
            path = ROOT/'src/activities/OpdsCatalogActivity.cpp'
        source = method(path.read_text(), '::onDownloadProgress(').replace('OpdsCatalogActivity::', 'CatalogActivity::')
        fixture = r"""
#include <cassert>
#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <cstddef>
using std::size_t;
std::mutex panel,model;
struct MappedInputManager {
 enum class Button { Back };
 int event=0, polls=0;
 void update(bool) { ++polls; }
 bool wasReleased(Button) {return event==1;}
 bool wasHomeGesture() {return event==2;}
};
struct Touch {bool touchReleased;};
Touch touchSnapshotFrom(MappedInputManager& i){return {i.event==3};}
unsigned millis(){return 10;}
struct State{};
bool xp_ui_catalog_progress(State*,size_t,size_t,unsigned){return false;}
struct CatalogActivity;
struct RenderLock {std::unique_lock<std::mutex> lock; RenderLock(CatalogActivity&):lock(panel){}};
struct ModelLock {std::unique_lock<std::mutex> lock; ModelLock(CatalogActivity&):lock(model){}};
struct RustActivityAdapter { bool cancelDownload=false; void loop(){cancelDownload=true;} };
struct CatalogActivity:RustActivityAdapter {
 State ui; MappedInputManager mappedInput; size_t downloadProgress=0,downloadTotal=0;
 bool goHomeAfterCancel=false;
 void requestUpdate(bool) {}
 void onDownloadProgress(size_t,size_t);
};
""" + source + r"""
int main(){
 for(int event=1;event<=3;event++) {
  CatalogActivity activity; activity.mappedInput.event=event;
  std::unique_lock<std::mutex> busy(panel);
  auto run=std::async(std::launch::async,[&]{activity.onDownloadProgress(123,456);});
  bool polled=run.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
  busy.unlock();run.get();
  assert(polled);assert(activity.mappedInput.polls==1);assert(activity.cancelDownload);
  assert(activity.goHomeAfterCancel==(event==2));
 }
}
"""
        self.compile_run(fixture)

    def test_growth_failure_can_retry_without_overlap(self):
        source = (ROOT/'src/activities/CatalogUiActivity.cpp').read_text()
        reserve = method(source, '::reserveCommands(')
        build = method(source, '::buildCatalogScene(')
        # The entry guards are the production retry path; later layout is mocked.
        build = build[:build.index('  XpUiCatalogState snapshot')] + '  return true;\n}'
        fixture = r"""
#include <cassert>
#include <algorithm>
#include <memory>
#include <cstdint>
#define LOG_ERR(...) ((void)0)
unsigned failAt=0,live=0,peak=0;
struct XpUiCommand { char bytes[32]; XpUiCommand(){peak=std::max(peak,++live);} ~XpUiCommand(){--live;} };
struct XpUiScene {};
template<class T> std::unique_ptr<T> makeUniqueNoThrow(unsigned n) {
 if(n==failAt)return {};
 return std::unique_ptr<T>(new typename std::remove_extent<T>::type[n]);
}
struct CatalogUiActivity {
 std::unique_ptr<XpUiCommand[]> commands;uint16_t capacity=0;
 int listCount(){return 14;}
 void prepareRows() {}
 bool reserveCommands(uint16_t);
 bool buildCatalogScene(XpUiScene&);
};
""" + reserve + build + r"""
int main(){
 CatalogUiActivity a; XpUiScene scene;
 failAt=48;assert(!a.reserveCommands(48));assert(!a.commands);assert(live==0);
 failAt=0;assert(a.reserveCommands(48));assert(live==48);
 failAt=64;assert(!a.reserveCommands(64));assert(live==0);
 failAt=0;assert(a.buildCatalogScene(scene));assert(live==64);assert(peak==64);
 assert(a.reserveCommands(128));assert(!a.reserveCommands(144));assert(live==128);
}
"""
        self.compile_run(fixture)

    def test_original_width_page_metadata_bridge(self):
        source = (ROOT/'src/activities/browser/OpdsBookBrowserActivity.h').read_text()
        declaration = method(source, 'pageNumbers(').replace(' const override', ' const')
        fixture = '#include <cassert>\n#include "xp_ui_catalog.h"\nstruct Browser { int pageCurrent,pageTotal;\n' + declaration + r"""
};
int main(){
 for(int current: {0,1,32767,32768,65535,65536,2147483647})
 for(int total: {0,1,32767,32768,65535,65536,2147483647}) {
  Browser browser{current,total};XpUiCatalogLayout layout{};
  browser.pageNumbers(layout.page_current,layout.page_total);
  assert(layout.page_current==current && layout.page_total==total);
 }
}
"""
        self.compile_run(fixture)

    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)/'fixture.cpp';path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['c++','-std=c++20','-pthread','-include','initializer_list','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

if __name__ == '__main__': unittest.main()
