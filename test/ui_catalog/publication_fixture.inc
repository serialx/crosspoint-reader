#include <cassert>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include "xp_ui_catalog.h"
#define LOG_ERR(...) ((void)0)
struct RenderLock {};
struct HalDisplay {enum {FAST_REFRESH};};
struct CatalogUiActivity;
struct ResourceLock {std::unique_lock<std::recursive_mutex> lock;explicit ResourceLock(CatalogUiActivity&);};
struct ModelLock {std::unique_lock<std::mutex> lock;explicit ModelLock(CatalogUiActivity&);};
struct CatalogUiActivity {
 std::recursive_mutex resources;std::mutex model;
 XpUiCatalogState ui{},paintState{};XpUiCatalogHits publishedHits{},nextHits{};
 XpUiListState next{};int32_t consumedScroll=0;
 bool sceneFailed=false,touchActivated=false,rejectedTouch=false;uint16_t rejectedButtons=0;
 struct {bool ready=false;} publication;
 std::string name="catalog";
 struct Renderer {
  bool pause=true;std::promise<void> painting,panel;std::shared_future<void> painted,refreshed;
  bool drawUiScene(XpUiScene&,const int*){if(pause){painting.set_value();painted.wait();}return true;}
  void displayBuffer(int){if(pause){panel.set_value();refreshed.wait();}}
 } renderer;
 CatalogUiActivity(){xp_ui_catalog_init(&ui,XP_UI_CATALOG_PLUGINS,-1,0);ui.mode=XP_UI_CATALOG_PLUGIN_PICKER;}
 int listCount(){return 1;}bool hasSearch(){return false;}uint8_t pageLinks(){return 0;}
 bool buildRustScene(XpUiScene&){
  // SNAPSHOT_CODE
  next=snapshot.list;nextHits={};
  if(snapshot.popup){
   nextHits.controls[0]={{200,0,100,100},XP_UI_CATALOG_HUB_OPTION,0,1,0};nextHits.control_count=1;
  }else{nextHits.rows=1;nextHits.row_area={0,0,100,100};nextHits.heights[0]=100;}
  return true;
 }
 int rustResources(){return 0;}void rejectScene(){assert(false);}
 void render(RenderLock&&);void rustPublished();void closeRouting();XpUiEffect updateRust(const XpUiInput&);
};
ResourceLock::ResourceLock(CatalogUiActivity& a):lock(a.resources){}
ModelLock::ModelLock(CatalogUiActivity& a):lock(a.model){}
// RENDER_METHODS
struct PluginCatalogActivity:CatalogUiActivity {
 enum class State {PLUGIN_PICKER};State getState(){return State::PLUGIN_PICKER;}
 bool showOpds=false,showPluginHubInstallRow=true;int updates=0;
 struct {void goToBrowser(){assert(false);}} activityManager;
 int rowCount(){return 1;}void requestUpdate(){++updates;}void activateIndex(int);
};
// ACTIVATE_METHOD
int main(){
 PluginCatalogActivity a;std::promise<void> painted,refreshed,opening;
 a.renderer.painted=painted.get_future().share();a.renderer.refreshed=refreshed.get_future().share();
 auto drawing=std::async(std::launch::async,[&]{a.render(RenderLock{});});
 a.renderer.painting.get_future().wait(); // picker snapshot exists; paint cannot yet publish
 auto opener=std::async(std::launch::async,[&]{opening.set_value();a.activateIndex(0);});
 opening.get_future().wait();
 const bool ordered=opener.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout;
 painted.set_value();a.renderer.panel.get_future().wait();
 const bool independent=opener.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
 if(!independent){refreshed.set_value();drawing.get();opener.get();assert(independent);}
 opener.get();
 bool open,ready;
 {
  ModelLock lock(a);assert(a.ui.popup==1);
  for(auto flags:{XP_UI_TOUCH_PRESSED,XP_UI_TOUCH_RELEASED}){
   XpUiInput input{};input.touch_flags=flags;input.touch_x=20;input.touch_y=20;a.updateRust(input);
  }
  open=a.ui.popup!=0;ready=a.publication.ready;
 }
 refreshed.set_value();drawing.get();
 assert(open && !ready); // stale picker hits cannot dismiss the unpublished popup
 assert(ordered && independent); // painting may block opening; panel refresh may not
 a.renderer.pause=false;a.render(RenderLock{});
 assert(a.publication.ready && a.ui.popup==1);
 XpUiInput input{};input.touch_flags=XP_UI_TOUCH_RELEASED;input.touch_x=220;input.touch_y=20;
 const auto effect=a.updateRust(input);
 assert(effect.kind==XP_UI_CATALOG_HUB_OPTION && effect.value==0 && a.ui.popup==0);
}
