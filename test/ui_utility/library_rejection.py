"""Real App adoption failures composed with the installed Library callbacks."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(path, signature):
    text = (ROOT / path).read_text()
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


manager = 'src/activities/ActivityManager.cpp'
text = (ROOT / manager).read_text()
signature = re.search(r'(?:void|bool) ActivityManager::pushActivity\(', text).group()
push = method(manager, signature)
source = r'''
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <xp_dialog.h>
#include <xp_ui_keyboard.h>
#include "ActivityResult.h"
#include "app_leaves.h"
#include "physical_defaults.h"
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_live_allocations();
extern "C" size_t xp_test_allocation_attempts();
extern "C" void* heap_caps_malloc(size_t, uint32_t);
extern "C" void heap_caps_free(void*);
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
struct GfxRenderer {};
struct MappedInputManager {};
struct DialogActivity;
int children=0, wrappers=0, swallowed=0, locks=0, updates=0;
bool locked=false, modelLocked=false;
struct Activity {
  std::string name;
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  ActivityResultHandler resultHandler;
  explicit Activity(std::string n):name(std::move(n)){}
  virtual ~Activity()=default;
  virtual DialogActivity* dialog(){return nullptr;}
  void startActivityForResult(std::unique_ptr<Activity>&&,ActivityResultHandler);
  void requestUpdate(bool=false){assert(!locked && !modelLocked);++updates;}
};
struct DialogActivity:Activity {
  using Activity::Activity;
  DialogActivity* dialog()override{return this;}
};
enum StrId { STR_REMOVE_FROM_RECENTS, STR_DELETE_ITEM_CONFIRM, STR_LIBRARY_REBUILDING };
const char* tr(StrId){return "Confirmation heading";}
struct ConfirmationActivity:DialogActivity {
  std::string heading,body;
  ConfirmationActivity(GfxRenderer&,MappedInputManager&,const char* h,std::string b)
      :DialogActivity("Confirmation"),heading(h),body(std::move(b)){}
  ~ConfirmationActivity(){assert(!locked && !modelLocked);++children;}
};
template<class T,class... A> auto makeUniqueNoThrow(A&&...a){
  if constexpr(std::is_same_v<T,OwnedLegacyActivity>)++wrappers;
  return std::unique_ptr<T>(new(std::nothrow)T(std::forward<A>(a)...));
}
// OWNER
struct ActivityManager { // DECLARATION
};
ActivityManager activityManager;
bool send(const XpAppEvent& e){return xp_app_event(&e)==XP_APP_ACCEPTED;}
// PUSH
// START
namespace library {
enum class SortOrder { Title };
struct ClixRecord {};
const char* libraryIndexPath(){return "/index";}
}
struct Index {
  bool opened=true;
  int selections=0;
  bool isOpen()const{return opened;}
  void close(){opened=false;}
  bool open(const char*){assert(locked);opened=true;return true;}
  uint16_t ordinalForRow(library::SortOrder,uint16_t row){assert(opened && row==0);++selections;return 0;}
  bool readRecord(uint16_t,library::ClixRecord&){assert(opened);return true;}
  bool readPath(const library::ClixRecord&,std::string& out){assert(opened);out="/book.epub";return true;}
};
struct Recent {
  bool removeByPath(const std::string&){assert(false && "cancel must not remove recent");return false;}
  void saveToFile(){assert(false);}
} RECENT_BOOKS;
struct Gui {void drawPopup(GfxRenderer&,const char*){assert(false);}} GUI;
enum {XP_UI_LIBRARY_CLOSE_REVEAL,XP_UI_LIBRARY_REBUILT};
struct LibraryListActivity:Activity {
  LibraryListActivity():Activity("Library"){}
  Index index;
  struct {int sort=0;} state;
  unsigned publication=17;
  void swallowHeldReleases(){
    assert(!locked && !modelLocked && !resultHandler && children==swallowed+1);
    assert(publication==17);++swallowed;
  }
  void promptDeleteBookByPath(const std::string&,const std::string&);
  void promptRemoveRecentBook(const std::string&,const std::string&);
  void promptDeleteBook(int);
  int bookRowCount(){return 1;}
  int rowFor(int n){return n;}
  void rowTextFor(int,std::string& title,std::string& author){title="A public book";author="Author";}
  bool removeBookFile(const std::string&){assert(false && "cancel must not delete");return false;}
  void resolvePinned(){assert(false);}
  void rebuildIndex(){assert(false);}
  void resetAfterRebuild(){assert(false);}
  int listCount(){return 1;}
  void event(int,int=0,int=0){assert(false);}
};
struct RenderLock {
  explicit RenderLock(Activity&){assert(!locked && !modelLocked);locked=true;++locks;}
  ~RenderLock(){assert(locked);locked=false;}
};
// LIBRARY
XpAppText loan(const std::string& s){return {reinterpret_cast<const uint8_t*>(s.data()),s.size()};}
extern "C" void xp_dialog_launch(void* p,XpDialogLaunch* out){
  auto* child=static_cast<ConfirmationActivity*>(nativeActivity(p));
  *out={.kind=XP_DIALOG_CONFIRMATION,.name=loan(child->name),.heading=loan(child->heading),.body=loan(child->body)};
}
extern "C" void xp_dialog_release_launch(void*){assert(false && "rejected child cannot be adopted");}
int main(){
  static auto legacy=appTestLegacy();
  static auto resources=appTestResources();
  static auto runtime=appTestRuntime();
  static auto epub=unexpectedEpubLeaves();
  legacy.destroy=[](void* p){delete static_cast<OwnedLegacyActivity*>(p);};
  resources.buffer_allocate=[](uint8_t domain,size_t n)->uint8_t*{assert(domain==0);return static_cast<uint8_t*>(heap_caps_malloc(n,0));};
  resources.buffer_release=[](uint8_t, uint8_t* p){heap_caps_free(p);};
  runtime.render_lock=[]()->void*{assert(!locked);locked=true;return &locked;};
  runtime.render_unlock=[](void*){assert(locked);locked=false;};
  runtime.model_lock=[](){assert(!modelLocked);modelLocked=true;};
  runtime.model_unlock=[](){assert(modelLocked);modelLocked=false;};
  runtime.ble_before_screen_change=[](uint8_t)->uint8_t{return 1;};
  XpAppCompatibility compatibility{};
  const XpAppPlatform platform{&legacy,&resources,&runtime,&epub};
  assert(xp_app_init(&platform,&compatibility)==XP_APP_OK);
  auto parent=std::make_unique<LibraryListActivity>();auto* library=parent.get();
  auto owner=ownLegacy(std::move(parent));auto* original=owner.get();
  XpAppEvent replace{.kind=XP_APP_ADOPT_REPLACE};replace.value.adopt_replace=owner.release();assert(send(replace));
  xp_app_loop();
  auto baseline=xp_test_live_allocations();
  for(bool recent:{false,true})for(size_t failure=0;failure<3;++failure){
    int before=children, oldWrappers=wrappers, oldUpdates=updates;
    xp_test_fail_allocation_after(failure);
    if(recent)library->promptRemoveRecentBook("/book.epub","A public book");
    else library->promptDeleteBookByPath("/book.epub","A public book");
    assert(children==before+1 && wrappers==oldWrappers+1);
    assert(library->index.isOpen() && !library->resultHandler);
    assert(swallowed==children && locks==children && updates==oldUpdates+1);
    assert(xp_test_allocation_attempts()==failure+1);
    assert(xp_test_live_allocations()==baseline);
    assert(xp_app_test_current_legacy()==original && xp_app_test_parent_count()==0 && xp_app_test_pending_kind()==0);
    assert(library->publication==17);
    // The real indexed-selection method must still resolve the next row.
    xp_test_fail_allocation_after(0);
    int selections=library->index.selections;
    library->promptDeleteBook(0);
    assert(library->index.selections==selections+1 && library->index.isOpen());
    assert(children==before+2 && swallowed==children && !library->resultHandler);
    assert(xp_app_test_pending_kind()==0 && xp_test_live_allocations()==baseline);
    xp_test_fail_allocation_after(SIZE_MAX);
  }
}
'''
# Keep the actual wrapper implementation, including its allocation and destructor.
owner = (ROOT / 'src/activities/OwnedLegacyActivity.h').read_text()
source = source.replace('template<class T,class... A>', 'struct OwnedLegacyActivity;\ntemplate<class T,class... A>')
source = source.replace('// OWNER', owner[owner.index('struct OwnedLegacyActivity {'):])
source = source.replace('// DECLARATION', push[:push.index('{')].replace('ActivityManager::', '') + ';')
source = source.replace('// PUSH', push)
source = source.replace('// START', method('src/activities/Activity.cpp', '__attribute__((noinline)) void cancelRejectedDialog(')
                        + '\n' + method('src/activities/Activity.cpp', 'void Activity::startActivityForResult('))
source = source.replace('// LIBRARY', '\n'.join(method('src/activities/library/LibraryListActivity.cpp', 'void LibraryListActivity::'+n+'(')
                                               for n in ['promptRemoveRecentBook', 'promptDeleteBookByPath', 'promptDeleteBook']))
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'rejection.cpp').write_text(source)
    subprocess.run(['c++', '-std=c++20', '-DXP_APP_TEST', '-I', str(ROOT / 'test/sdfont_legacy/abi'), '-pthread', '-UNDEBUG', '-DXP_FFI_HOST_FAULT_INJECTION',
                    '-I'+str(ROOT/'src/activities'), '-I'+str(ROOT/'test/ui_runtime'),
                    '-I'+str(ROOT/'test/reader_lifecycle'), '-I'+str(ROOT/'rust/crates/xp-ffi/include'),
                    str(path/'rejection.cpp'), str(ROOT/'test/cmake/xp_ffi_host.cpp'), sys.argv[1],
                    '-o', str(path/'rejection')], check=True)
    subprocess.run([str(path/'rejection')], check=True)
print('Library delete/recent rejection: real Rust shell + two text failures; 12 callback/index selections')
