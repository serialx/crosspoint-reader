"""Production catalog callbacks, linked to the real Rust state machine.

CATALOG_SOURCE_REVISION replays the C++ callbacks from a Git revision so the
regressions can also be demonstrated against the pre-fix firmware.
"""
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

from test_adapter import method

ROOT = pathlib.Path(__file__).resolve().parents[2]
LIBRARY = pathlib.Path(sys.argv.pop(1)) if len(sys.argv) > 1 and sys.argv[1].endswith('.a') else next(
    (ROOT / 'build/test/xp-ffi-host-target').glob('*/release/libxp_ffi.a'))


def source(path):
    revision = os.environ.get('CATALOG_SOURCE_REVISION')
    if revision:
        return subprocess.check_output(['git', 'show', f'{revision}:{path}'], cwd=ROOT, text=True)
    return (ROOT / path).read_text()


class Review(unittest.TestCase):
    def compile_run(self, code):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'fixture.cpp'
            path.write_text(code)
            binary = path.with_suffix('')
            subprocess.run(['c++', '-std=c++20', '-pthread', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', '-I' + str(ROOT / 'rust/crates/xp-ffi/include'),
                            '-I' + str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
                            '-I' + str(ROOT / 'test/ui_catalog'),
                            str(path), str(ROOT / 'test/cmake/xp_ffi_host.cpp'), str(LIBRARY),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_popup_open_orders_publication_without_waiting_for_panel(self):
        ui = source('src/activities/CatalogUiActivity.cpp')
        plugin = source('src/activities/plugins/PluginCatalogActivity.cpp')
        render = '\n'.join(method(ui, '::' + name + '(') for name in
                           ['render', 'updateRust', 'rustPublished', 'closeRouting'])
        activate = method(plugin, '::activateIndex(')
        # Keep the production picker/Hub dispatch; unrelated plugin loading is
        # outside this fixture's service boundary.
        activate = activate[:activate.index('    const int syntheticHubRows')] + '  }\n}'
        build = method(ui, '::buildCatalogScene(')
        snapshot = build[build.index('  XpUiCatalogState snapshot{};'):build.index('  prepareRows();')]
        fixture = (ROOT / 'test/ui_catalog/publication_fixture.inc').read_text()
        self.compile_run(fixture.replace('// RENDER_METHODS', render)
                         .replace('// ACTIVATE_METHOD', activate)
                         .replace('// SNAPSHOT_CODE', snapshot))

    def test_server_reload_cannot_publish_old_row_hits(self):
        ui = source('src/activities/CatalogUiActivity.cpp')
        server = source('src/activities/settings/OpdsServerListActivity.cpp')
        adapter = source('src/activities/RustActivityAdapter.cpp')
        fixture = (ROOT / 'test/ui_catalog/publication_fixture.inc').read_text().split('struct PluginCatalogActivity:')[0]
        fixture = fixture.replace('struct HalDisplay {enum {FAST_REFRESH};};',
                                  'struct HalDisplay {enum {FAST_REFRESH,HALF_REFRESH,FULL_REFRESH};};')
        fixture = fixture.replace('struct {bool ready=false;} publication;',
                                  'struct Publication {bool ready=false;XpUiScene scene{};} publication;\n'
                                  'Publication* rustPublication(){return &publication;}')
        fixture = fixture.replace('bool buildRustScene(XpUiScene&){',
                                  'bool buildRustScene(XpUiScene& scene){scene.refresh=XP_UI_FAST;')
        build = method(ui, '::buildCatalogScene(')
        snapshot = build[build.index('  XpUiCatalogState snapshot{};'):build.index('  prepareRows();')]
        fixture = fixture.replace('// SNAPSHOT_CODE', snapshot).replace('// RENDER_METHODS', '\n'.join(
            method(ui, '::' + name + '(') for name in ['render', 'updateRust', 'rustPublished', 'closeRouting']))
        callback = method(server, '::handleSelection(').split('  auto resultHandler = ')[1].split('\n  };', 1)[0] + '\n  };'
        fixture += r'''
#include <utility>
struct ActivityResult {};
struct {void loadFromFile(){}} OPDS_STORE;
struct RustActivityAdapter:CatalogUiActivity {
 void render(RenderLock&&);bool paintRustScene(const XpUiScene&);
 virtual void rustSceneFailed() {}
 bool rustSceneCurrent()const{return true;}void requestUpdate(){}
};
'''
        fixture = fixture.replace('bool drawUiScene(XpUiScene&,const int*)', 'bool drawUiScene(const XpUiScene&,const int*)')
        if 'void RustActivityAdapter::rejectRustScene()' in adapter:
            fixture = fixture.replace('bool paintRustScene(const XpUiScene&);',
                                      'bool paintRustScene(const XpUiScene&, bool publish=true);\n'
                                      'void rejectRustScene();void publishRustScene(const XpUiScene&);\n'
                                      'uint32_t suppressButtons=0,lastHeld=0;\n'
                                      'bool sceneFailed=false,suppressContact=false,lastContact=false;')
            fixture += method(adapter, '::rejectRustScene(') + '\n' + method(adapter, '::publishRustScene(')
        fixture += method(adapter, '::render(') + '\n' + method(adapter, '::paintRustScene(')
        fixture += r'''
struct OpdsServerListActivity:RustActivityAdapter {
 struct {bool processRender(Renderer&,int){return false;}} optionPopup;
 int mappedInput=0;
 void render(RenderLock&&);void resetSelection(){}
 void reload(){auto resultHandler = ''' + callback + r''' resultHandler(ActivityResult{});}
};
''' + method(server, '::render(') + r'''
int main(){
 OpdsServerListActivity a;std::promise<void> painted,refreshed,started;
 a.renderer.painted=painted.get_future().share();a.renderer.refreshed=refreshed.get_future().share();
 auto drawing=std::async(std::launch::async,[&]{a.render(RenderLock{});});
 a.renderer.painting.get_future().wait();
 auto reload=std::async(std::launch::async,[&]{started.set_value();a.reload();});
 started.get_future().wait();
 const bool ordered=reload.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout;
 painted.set_value();a.renderer.panel.get_future().wait();
 const bool independent=reload.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
 if(!independent){refreshed.set_value();drawing.get();reload.get();assert(independent);}
 reload.get();bool ready;{ModelLock lock(a);ready=a.publication.ready;}
 refreshed.set_value();drawing.get();assert(!ready && ordered);
}
'''
        self.compile_run(fixture)

    def test_sdk_contacts_retain_original_owner(self):
        sdk = (ROOT / 'freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp').read_text()
        mapped = source('src/MappedInputManager.cpp')
        ui = source('src/activities/CatalogUiActivity.cpp')
        snapshot = method(source('src/components/UiAppHelpers.h'), 'inline freeink::ui::InputSnapshot touchSnapshotFrom(')
        sdk_methods = '\n'.join(method(sdk, '::' + name + '(') for name in
                                ['serviceTouch', 'wasTouchTap', 'wasTouchLongPress', 'suppressTouchContact',
                                 'isTouchTapCandidate', 'isTouchHeldAt', 'wasTouchReleased'])
        mapped_methods = '\n'.join(method(mapped, '::' + name + '(') for name in
                                   ['wasScreenTapped', 'wasScreenLongPress', 'wasScreenTouchDown',
                                    'isScreenTouchHeld', 'wasScreenTouchReleased'])
        callbacks = '\n'.join(method(ui, '::' + name + '(') for name in ['loop', 'updateRust'])
        fixture = (ROOT / 'test/ui_catalog/touch_fixture.inc').read_text()
        self.compile_run(fixture.replace('// SDK_METHODS', sdk_methods)
                         .replace('// MAPPED_METHODS', mapped_methods)
                         .replace('// SNAPSHOT_METHOD', snapshot)
                         .replace('// CATALOG_METHODS', callbacks))

    def test_unauthorized_back_follows_production_callback(self):
        plugin = source('src/activities/plugins/PluginCatalogActivity.cpp')
        methods = '\n'.join(method(plugin, '::' + name + '(') for name in
                            ['fetchBrowseResponse', 'performSearch', 'onBackButton', 'exitCatalog'])
        self.compile_run(r'''
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include "xp_ui_catalog.h"
constexpr auto BROWSE_TMP_PATH="browse";
constexpr unsigned MAX_BROWSE_RESPONSE=1;
namespace pluginhttp {
struct RequestSpec {std::string url,method,body; std::vector<int> headers;};
int status=200,requests=0;
int requestToFile(void*,const std::string&,const std::string&,const std::string&,
                  const std::vector<int>&,const char*,unsigned){++requests;return status;}
}
struct {void remove(const char*){}} Storage;
struct StrId {enum {STR_PLUGIN_MANIFEST_INVALID,STR_FETCH_FEED_FAILED};};
struct PluginCatalogActivity;
struct ResourceLock {explicit ResourceLock(PluginCatalogActivity&) {}};
struct ModelLock {explicit ModelLock(PluginCatalogActivity&) {}};
struct PluginCatalogActivity {
 enum class State {CHECK_WIFI,WIFI_SELECTION,SEARCH_INPUT,LOADING,BROWSING,DOWNLOADING,
                  ERROR,PLUGIN_PICKER,LIST_PICKER,DONE,NO_TOKEN,AUTH};
 XpUiCatalogState ui{};
 struct Manifest {
  bool xml=false; std::string xmlItem="item";
  pluginhttp::RequestSpec browseReq{};std::vector<int> browseLists;
  bool hasPasswordGrant(){return false;}bool isXmlList(){return xml;}
 } manifest;
 std::unique_ptr<int> session;
 bool rootMode=false,searchActive=false;int starts=0,auth=0,exits=0;
 std::string searchQuery,browseCurrentUrl;std::vector<std::string> browseHistory;
 PluginCatalogActivity(){xp_ui_catalog_init(&ui,XP_UI_CATALOG_PLUGINS,-1,0);}
 State getState(){return static_cast<State>(ui.mode);}
 void setState(State s){xp_ui_catalog_mode(&ui,static_cast<uint8_t>(s));}
 void requestUpdate(){}void loadConfig(){}bool loadToken(){return true;}
 bool refreshCredentialToken(){++auth;return false;}
 void fail(int){assert(false);}
 std::string substituted(const std::string& s,void*){return s;}
 std::vector<int> substitutedHeaders(const std::vector<int>& h,void*){return h;}
 std::string activeBrowseUrl(){return "search";}std::string activeBrowseBody(){return {};}
 void releaseRows(){}void resetSelection(){}
 void beginLoading(){setState(State::LOADING);}
 void fetchPage(int){fetchBrowseResponse();}
 void startBrowse(){++starts;fetchPage(1);}
 void enterPluginPicker(){++exits;setState(State::PLUGIN_PICKER);}
 void onGoHome(){assert(false);}void finish(){assert(false);}
 bool fetchBrowseResponse();void performSearch(const std::string&);void onBackButton();void exitCatalog();
 void back(){XpUiInput i{};i.released=XP_UI_BACK;
  auto e=xp_ui_catalog_update(&ui,&i,2,1,0);if(e.kind==XP_UI_CANCEL)onBackButton();}
};
''' + methods + r'''
int main(){
 for(int status:{401,403})for(bool xml:{false,true}){
  PluginCatalogActivity a;pluginhttp::status=status;a.manifest.xml=xml;
  if(xml){a.browseHistory={"root"};a.browseCurrentUrl="child";a.fetchBrowseResponse();}
  else a.performSearch("query");
  assert(a.getState()==PluginCatalogActivity::State::NO_TOKEN);
  const int requests=pluginhttp::requests;a.back();
  assert(a.getState()==PluginCatalogActivity::State::PLUGIN_PICKER);
  assert(a.exits==1 && a.starts==0 && a.auth==0 && pluginhttp::requests==requests);
 }
 PluginCatalogActivity auth;auth.setState(PluginCatalogActivity::State::AUTH);auth.back();
 assert(auth.getState()==PluginCatalogActivity::State::NO_TOKEN && auth.exits==0);
 PluginCatalogActivity search;pluginhttp::status=200;search.performSearch("query");
 search.setState(PluginCatalogActivity::State::BROWSING);search.back();
 assert(!search.searchActive && search.searchQuery.empty() && search.starts==1 && search.exits==0);
}
''')


if __name__ == '__main__':
    unittest.main()
