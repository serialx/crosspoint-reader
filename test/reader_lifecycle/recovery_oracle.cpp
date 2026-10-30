#include <xp_ui_reader.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
static void event(const char* name, int value = 0) { std::printf("%s %d\n", name, value); }
enum StrId {
  STR_DRM_PROTECTED_FILE,
  STR_LOAN_EXPIRED,
  STR_LOAN_TIME_UNVERIFIED,
  STR_CLOCK_SYNC_NOW,
  STR_OK_BUTTON,
  STR_SYNCING_TIME,
  STR_RETRY,
  STR_CLOCK_SYNC_FAIL
};
struct I18n {
  const char* get(StrId id) {
    event("translation", id);
    static const char* names[] = {"DRM", "expired", "unverified", "sync", "ok", "syncing", "retry", "failed"};
    return names[id];
  }
} I18N;
const char* tr(StrId id) { return I18N.get(id); }
#define LOG_ERR(tag, message) std::printf("error [%s] [%s]\n", tag, message)
static int locks = 0;
struct RenderLock {
  RenderLock() { event("lock", ++locks); }
  ~RenderLock() { event("unlock", locks--); }
};
struct Renderer {
  void clearScreen() { event("clear", locks); }
};
struct Input {};
struct ActivityResult {
  bool isCancelled;
};
struct WifiSelectionActivity {
  WifiSelectionActivity(Renderer&, Input&) { event("wifi-new"); }
  ~WifiSelectionActivity() { event("wifi-drop"); }
};
static bool allocation = true;
template <class T, class... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  event("allocate", allocation);
  if (!allocation) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}
constexpr int WL_CONNECTED = 3;
struct Wifi {
  int state = 0;
  int status() {
    event("wifi-status", state);
    return state;
  }
  void disconnect(bool value) { event("disconnect", value); }
} WiFi;
static bool syncOk = false;
namespace trustedtime {
bool syncNow(int timeout) {
  event("sync", timeout);
  return syncOk;
}
}  // namespace trustedtime
void delay(int value) { event("delay", value); }
struct Gui {
  void drawPopup(Renderer&, const char* message) { std::printf("draw [%s]\n", message); }
} GUI;
struct State {
  std::string openEpubPath;
  void saveToFile() { std::printf("save [%s]\n", openEpubPath.c_str()); }
} APP_STATE;
void silentRestartToReader() { event("restart"); }
struct Manager {
  void goToReader(const std::string& path) { std::printf("go [%s]\n", path.c_str()); }
};
static Manager activityManager;
struct Popup {
  bool active = false;
  std::function<void(int)> callback;
  bool isActive() {
    event("active", active);
    return active;
  }
  void processRender(Renderer&, Input&) { event("popup-render", locks); }
  void showMessage(const char* title, const char* message, const char** options, int count, int selected,
                   std::function<void(int)> cb) {
    std::printf("message [%s] [%s] %d %d\n", title, message, count, selected);
    for (int i = 0; i < count; ++i) std::printf("option [%s]\n", options[i]);
    active = true;
    callback = std::move(cb);
  }
};
struct ReaderActivity {
  void render(RenderLock&&) { event("base-render", locks); }
};
struct EpubReaderActivity : ReaderActivity {
  enum class Overlay { None };
  Overlay overlay = Overlay::None;
  void publishRustScene(const XpUiScene&) {}
  Renderer renderer;
  Input mappedInput;
  Popup loadFailurePopup;
  std::string loadProtectionError, bookPath = "original.epub";
  std::unique_ptr<WifiSelectionActivity> child;
  std::function<void(const ActivityResult&)> childResult;
  void startActivityForResult(std::unique_ptr<WifiSelectionActivity> activity,
                              std::function<void(const ActivityResult&)> callback) {
    event("start-child");
    child = std::move(activity);
    childResult = std::move(callback);
  }
  void finish() { event("finish"); }
  void requestUpdate() { event("update"); }
  bool runRecoveryPolicy(uint8_t operation, int32_t a = 0, int32_t b = 0, const ActivityResult* result = nullptr);
  bool handleLoadFailure();
  void beginLoanTimeSync();
  void render(RenderLock&&);
};
// RECOVERY_METHODS
static void choose(EpubReaderActivity& owner, int index) {
  if (!owner.loadFailurePopup.callback) return;
  auto callback = std::move(owner.loadFailurePopup.callback);
  owner.loadFailurePopup = Popup{};
  event("popup-dismiss");
  callback(index);
}
int main() {
  const char* errors[] = {"", "access expired", "loan date unverified", "unknown"};
  for (int error = 0; error < 4; ++error)
    for (int alloc = 0; alloc < 2; ++alloc)
      for (int cancel = 0; cancel < 2; ++cancel)
        for (int connected : {0, 3, 7})
          for (int sync = 0; sync < 2; ++sync)
            for (int first : {-1, 0, 1, 2})
              for (int retry : {-1, 0, 1, 2}) {
                std::printf("case %d %d %d %d %d %d %d\n", error, alloc, cancel, connected, sync, first, retry);
                allocation = alloc;
                WiFi.state = connected;
                syncOk = sync;
                APP_STATE.openEpubPath = "before";
                EpubReaderActivity owner;
                owner.loadProtectionError = errors[error];
                event("handled", owner.handleLoadFailure());
                owner.loadProtectionError = "changed after show";
                choose(owner, first);
                if (owner.childResult) {
                  auto callback = std::move(owner.childResult);
                  owner.child.reset();
                  event("child-dismiss");
                  owner.bookPath = "changed-before-result.epub";
                  callback(ActivityResult{bool(cancel)});
                  choose(owner, retry);
                }
                std::printf("state [%s]\n", APP_STATE.openEpubPath.c_str());
              }
  for (bool active : {false, true}) {
    EpubReaderActivity owner;
    owner.loadFailurePopup.active = active;
    {
      RenderLock lock;
      owner.render(std::move(lock));
      assert(locks == 1);
    }
    assert(locks == 0);
  }
}
