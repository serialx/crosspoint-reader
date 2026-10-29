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
        source = method(path.read_text(), '::onDownloadProgress(')
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

    def test_resource_lifetime_and_panel_independence(self):
        source = (ROOT/'src/activities/CatalogUiActivity.cpp').read_text()
        render = method(source, '::render(')
        fixture = r"""
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
struct XpUiScene {};
struct RenderLock {};
struct HalDisplay {enum {FAST_REFRESH};};
struct CatalogUiActivity;
struct ResourceLock {std::unique_lock<std::recursive_mutex> lock;ResourceLock(CatalogUiActivity&);};
struct ModelLock {explicit ModelLock(CatalogUiActivity&) {}};
struct Renderer {
 std::promise<void> painting,panel;
 std::shared_future<void> painted,refreshed;
 const char* text=nullptr;
 bool drawUiScene(XpUiScene&,const int*) {
  painting.set_value(); painted.wait(); assert(std::string(text)==std::string(4096,'a')); return true;
 }
 void displayBuffer(int) {panel.set_value();refreshed.wait();}
};
struct CatalogUiActivity {
 std::recursive_mutex resources;
 Renderer renderer;
 std::vector<std::string> rows{std::string(4096,'a')};
 struct {bool ready=false;} publication;
 bool sceneFailed=false;
 std::string name="catalog";
 bool buildRustScene(XpUiScene&) {renderer.text=rows[0].c_str();return true;}
 int rustResources() {return 0;}
 void rustPublished() {}
 void rejectScene() {sceneFailed=true;publication.ready=false;}
 void render(RenderLock&&);
};
ResourceLock::ResourceLock(CatalogUiActivity& a):lock(a.resources) {}
""" + render + r"""
int main(){
 for(int mutation=0;mutation<3;++mutation){
  CatalogUiActivity a;std::promise<void> painted,refreshed;
  a.renderer.painted=painted.get_future().share();a.renderer.refreshed=refreshed.get_future().share();
  auto drawing=std::async(std::launch::async,[&]{a.render(RenderLock{});});
  a.renderer.painting.get_future().wait();
  auto change=std::async(std::launch::async,[&]{ResourceLock lock(a);
   if(mutation==0)a.rows={std::string(8192,'b')};
   else if(mutation==1)std::vector<std::string>().swap(a.rows);
   else {auto selected=std::move(a.rows[0]);std::vector<std::string>().swap(a.rows);assert(selected.size()==4096);}
  });
  assert(change.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout);
  painted.set_value();a.renderer.panel.get_future().wait();
  const bool independent=change.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
  refreshed.set_value();change.get();drawing.get();assert(independent);
 }
}
"""
        self.compile_run(fixture, sanitize=True)

    def test_failed_scene_only_allows_back_and_retry(self):
        source = method((ROOT/'src/activities/CatalogUiActivity.cpp').read_text(), '::updateRust(')
        source = source[:source.index('  auto input = source;')] + '  return {};\n}'
        fixture = '#include <cassert>\n#include "xp_ui_catalog.h"\nextern "C" XpUiEffect xp_ui_catalog_update(XpUiCatalogState*,const XpUiInput* input,uint16_t,uint8_t,uint8_t){XpUiEffect e{};e.kind=input->released & XP_UI_BACK ? XP_UI_CANCEL : XP_UI_ACTIVATE;return e;}\nstruct CatalogUiActivity {bool touchActivated=true,sceneFailed=true;XpUiCatalogState ui{};int listCount(){return 3;}bool hasSearch(){return false;}uint8_t pageLinks(){return 0;}XpUiEffect updateRust(const XpUiInput&);};\n' + source + r"""
int main(){CatalogUiActivity a;XpUiInput input{};input.released=XP_UI_CONFIRM;
 auto effect=a.updateRust(input);assert(effect.kind==XP_UI_EFFECT_NONE && effect.invalidate && !a.touchActivated);
 input.released=XP_UI_BACK;effect=a.updateRust(input);assert(effect.kind==XP_UI_CANCEL);
}
"""
        self.compile_run(fixture)

    def test_auth_poll_does_not_wait_for_panel(self):
        source = method((ROOT/'src/activities/plugins/PluginCatalogActivity.cpp').read_text(), '::pollAuth(')
        # ESP32 long is 32-bit; the host's long is 64-bit.
        source = source[:source.index('  JsonDocument doc;')].replace('static_cast<long>', 'static_cast<int32_t>') + '}\n'
        fixture = r"""
#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
std::mutex panel,model;
using String=std::string;
struct StrId {enum {STR_PLUGIN_AUTH_FAILED};};
unsigned millis(){return 10;}
struct Request {std::string url,body;};
void substituteAll(std::string&,const char*,const std::string&){}
struct PluginCatalogActivity;
struct RenderLock {std::unique_lock<std::mutex> lock;RenderLock(PluginCatalogActivity&):lock(panel){}};
struct ModelLock {std::unique_lock<std::mutex> lock;ModelLock(PluginCatalogActivity&):lock(model){}};
struct PluginCatalogActivity {
 unsigned authDeadlineMs=1000,authIntervalMs=500;
 struct {unsigned auth_next_poll_ms=0;} ui;
 struct {Request pollReq;} manifest;
 std::string authDeviceCode;
 bool requested=false;
 void fail(int){assert(false);}
 Request substitutedRequest(const Request& r){return r;}
 int apiRequest(Request,String&){requested=true;return -1;}
 void pollAuth();
};
""" + source + r"""
int main(){PluginCatalogActivity a;std::unique_lock<std::mutex> busy(panel);
 auto run=std::async(std::launch::async,[&]{a.pollAuth();});
 bool polled=run.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
 busy.unlock();run.get();assert(polled && a.requested && a.ui.auth_next_poll_ms==510);
}
"""
        self.compile_run(fixture)

    def test_sdk_long_press_routes_only_published_rows(self):
        cpp = (ROOT/'src/activities/CatalogUiActivity.cpp').read_text()
        source = method(cpp, '::updateRust(') + '\n' + method(cpp, '::rustPublished(')
        fixture = r"""
#include <cassert>
#include "xp_ui_catalog.h"
XpUiInput received{};
extern "C" void xp_ui_catalog_publish(XpUiCatalogState*,const XpUiListState*,int32_t) {}
extern "C" uint8_t xp_ui_catalog_hit(const XpUiCatalogHits*,int16_t x,int16_t,XpUiInteraction* hit) {
 if(x<0 || x==500)return 0;hit->action=x<100?1:XP_UI_CATALOG_SEARCH;hit->value=4;return 1;
}
extern "C" XpUiEffect xp_ui_catalog_update(XpUiCatalogState* state,const XpUiInput* input,uint16_t,uint8_t,uint8_t){
 received=*input;XpUiEffect effect{};
 if(state->popup && !input->action && (input->touch_flags & XP_UI_TOUCH_RELEASED)){state->popup=0;effect.invalidate=1;}
 if(input->action==XP_UI_CATALOG_UNINSTALL && input->touch_flags) {effect.kind=XP_UI_CATALOG_UNINSTALL;effect.value=input->value;}
 return effect;
}
struct CatalogUiActivity {
 bool sceneFailed=false,touchActivated=false,rejectedTouch=false;uint16_t rejectedButtons=0;
 struct {bool ready=true;} publication;
 XpUiCatalogState ui{};XpUiCatalogHits publishedHits{},nextHits{};XpUiListState next{};int32_t consumedScroll=0;
 void rustPublished();
 int listCount(){return 8;}bool hasSearch(){return true;}uint8_t pageLinks(){return 0;}
 XpUiEffect updateRust(const XpUiInput&);
};
""" + source + r"""
int main(){CatalogUiActivity a;XpUiInput input{};input.action=XP_UI_CATALOG_UNINSTALL;
 input.touch_flags=XP_UI_TOUCH_RELEASED;input.touch_x=20;
 auto effect=a.updateRust(input);assert(effect.kind==XP_UI_CATALOG_UNINSTALL && effect.value==4 && a.touchActivated);
 for(int x:{-1,200}){input.touch_x=x;effect=a.updateRust(input);assert(effect.kind==XP_UI_EFFECT_NONE && !received.touch_flags);}
 input.touch_x=20;a.ui.popup=1;effect=a.updateRust(input);assert(effect.kind==XP_UI_EFFECT_NONE && !received.touch_flags);
 a.ui.popup=0;a.publication.ready=false;effect=a.updateRust(input);assert(effect.kind==XP_UI_EFFECT_NONE && !received.touch_flags);
 a.sceneFailed=true;effect=a.updateRust(input);assert(effect.kind==XP_UI_EFFECT_NONE && effect.invalidate);
 a.rustPublished();a.publication.ready=true;effect=a.updateRust(input);assert(effect.kind==XP_UI_CATALOG_UNINSTALL && !a.sceneFailed);
 a.ui.popup=1;input.action=0;input.touch_x=500;effect=a.updateRust(input);assert(effect.invalidate && a.touchActivated && !a.ui.popup);
}
"""
        self.compile_run(fixture)

    def test_build_and_paint_rejection_before_and_after_publication(self):
        cpp = (ROOT/'src/activities/CatalogUiActivity.cpp').read_text()
        production = '\n'.join(method(cpp, '::'+name+'(') for name in
                               ['render','buildRustScene','rejectScene','updateRust','rustPublished','rustInterest'])
        fixture = r"""
#include <cassert>
#include <string>
#include "xp_ui_catalog.h"
#define LOG_ERR(...) ((void)0)
struct RenderLock {};
struct HalDisplay {enum {FAST_REFRESH};};
struct CatalogUiActivity;
struct ResourceLock {explicit ResourceLock(CatalogUiActivity&) {}};
struct ModelLock {explicit ModelLock(CatalogUiActivity&) {}};
extern "C" void xp_ui_catalog_publish(XpUiCatalogState*,const XpUiListState*,int32_t) {}
extern "C" XpUiInterest xp_ui_catalog_interest(const XpUiCatalogState*,uint32_t) {return {};}
extern "C" uint8_t xp_ui_catalog_hit(const XpUiCatalogHits*,int16_t,int16_t,XpUiInteraction* h) {
 h->action=1;h->value=2;return 1;
}
extern "C" XpUiEffect xp_ui_catalog_update(XpUiCatalogState* s,const XpUiInput* i,uint16_t,uint8_t,uint8_t) {
 XpUiEffect e{};
 if(i->released & XP_UI_BACK)e.kind=XP_UI_CANCEL;
 else if((i->released & XP_UI_CONFIRM) || (i->touch_flags & XP_UI_TOUCH_RELEASED) ||
         (i->held & XP_UI_CONFIRM))e.kind=XP_UI_ACTIVATE;
 else if((i->pressed | i->held) & XP_UI_NAV_NEXT)++s->list.selected;
 return e;
}
struct CatalogUiActivity {
 struct {bool ready=false;} publication;
 struct Renderer {bool paint=true;int refreshes=0;
  bool drawUiScene(XpUiScene&,const int*){return paint;}void displayBuffer(int){++refreshes;}
 } renderer;
 bool sharedRejected=false;
 void rejectRustScene(){sharedRejected=true;rejectScene();}
 void publishRustScene(const XpUiScene&){sharedRejected=false;rustPublished();publication.ready=true;}
 bool build=true,sceneFailed=false,touchActivated=false,rejectedTouch=false;
 uint16_t rejectedButtons=0;std::string name="catalog";
 XpUiCatalogState ui{};XpUiCatalogHits publishedHits{},nextHits{};XpUiListState next{};int32_t consumedScroll=0;
 bool buildCatalogScene(XpUiScene&){return build;}int rustResources(){return 0;}
 int listCount(){return 8;}bool hasSearch(){return true;}uint8_t pageLinks(){return 7;}
 void render(RenderLock&&);bool buildRustScene(XpUiScene&);void rejectScene();void rustPublished();
 XpUiEffect updateRust(const XpUiInput&);XpUiInterest rustInterest(uint32_t)const;
};
""" + production + r"""
int main(){
 for(int kind=0;kind<=4;++kind)for(bool first:{false,true})for(bool paint:{false,true}){
  CatalogUiActivity a;a.ui.kind=kind;XpUiInput input{};input.released=XP_UI_CONFIRM;
  assert(a.updateRust(input).kind==XP_UI_ACTIVATE); // original pre-first-paint behavior
  if(first)a.render(RenderLock{});
  a.ui.list.active=3;a.ui.list.pending_scroll=9;a.ui.list.last_repeat=500;
  a.ui.search_pending=a.ui.long_press=1;
  a.build=paint;a.renderer.paint=!paint;const auto refreshes=a.renderer.refreshes;
  a.render(RenderLock{});assert(a.sceneFailed && !a.publication.ready);assert(a.sharedRejected);
  assert(a.renderer.refreshes==refreshes && a.ui.list.active==-1 && !a.ui.list.pending_scroll);
  assert(!a.ui.list.last_repeat && !a.ui.search_pending && !a.ui.long_press);
  input.pressed=XP_UI_NAV_NEXT;input.held=XP_UI_CONFIRM;input.held_ms=1200;
  input.touch_flags=XP_UI_TOUCH_RELEASED;input.action=XP_UI_CATALOG_UNINSTALL;
  input.swipe=XP_UI_SWIPE_UP;
  const auto selected=a.ui.list.selected;
  auto effect=a.updateRust(input);assert(!effect.kind && effect.invalidate && !a.touchActivated);
  assert(a.ui.list.selected==selected);
  input={};input.released=XP_UI_BACK;assert(a.updateRust(input).kind==XP_UI_CANCEL);
  a.build=a.renderer.paint=true;a.render(RenderLock{});assert(!a.sceneFailed && a.publication.ready);
  assert(a.rustInterest(0).pressed & XP_UI_CONFIRM);
  input={};input.held=XP_UI_CONFIRM;input.held_ms=1200;input.touch_flags=XP_UI_TOUCH_HELD;
  assert(!a.updateRust(input).kind);
  input.held=0;input.released=XP_UI_CONFIRM;input.touch_flags=XP_UI_TOUCH_RELEASED;
  assert(!a.updateRust(input).kind); // stale release is consumed
  input={};input.pressed=XP_UI_CONFIRM;assert(!a.updateRust(input).kind);
  input.pressed=0;input.released=XP_UI_CONFIRM;assert(a.updateRust(input).kind==XP_UI_ACTIVATE);
  input={};input.touch_flags=XP_UI_TOUCH_PRESSED;assert(!a.updateRust(input).kind);
  input.touch_flags=XP_UI_TOUCH_RELEASED;assert(a.updateRust(input).kind==XP_UI_ACTIVATE);
 }
}
"""
        self.compile_run(fixture, sanitize=True)

    def compile_run(self, source, sanitize=False):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)/'fixture.cpp';path.write_text(source)
            binary = path.with_suffix('')
            subprocess.run(['c++'] + (['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if sanitize else []) + ['-std=c++20','-pthread','-include','initializer_list','-I'+str(ROOT/'rust/crates/xp-ffi/include'),str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

if __name__ == '__main__': unittest.main()
