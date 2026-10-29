#include <xp_ui_updates.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define tr(x) #x
namespace HalDisplay {
enum { FAST_REFRESH, HALF_REFRESH, FULL_REFRESH };
}
namespace freeink::ui {
struct ListItem {
  const char *label = nullptr, *subtitle = nullptr, *value = nullptr;
  int actionValue = 0;
};
}  // namespace freeink::ui
namespace fui = freeink::ui;
struct Gate {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, released = false;
  void stop() {
    std::unique_lock lock(mutex);
    entered = true;
    cv.notify_all();
    cv.wait(lock, [&] { return released; });
  }
  void wait() {
    std::unique_lock lock(mutex);
    assert(cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
  }
  void go() {
    std::lock_guard lock(mutex);
    released = true;
    cv.notify_all();
  }
};
static Gate rowsGate, panelGate;
static bool pauseRows = true, pausePanel = true;
static std::recursive_mutex resourceMutex;
static std::atomic<bool> resourceBlocked = false;
struct UpdateUiActivity;
struct Renderer {
  UpdateUiActivity* owner;
  bool drawUiScene(const XpUiScene&, const XpUiPaint*);
  void displayBuffer(int) {
    if (pausePanel) panelGate.stop();
  }
};
struct UpdateUiActivity {
  struct RenderLock {};
  struct ModelLock {
    std::unique_lock<std::mutex> lock;
    explicit ModelLock(UpdateUiActivity& a) : lock(a.modelMutex) {}
  };
  struct ResourceLock {
    ResourceLock(UpdateUiActivity&) {
      if (!resourceMutex.try_lock()) {
        resourceBlocked = true;
        resourceMutex.lock();
      }
    }
    ~ResourceLock() { resourceMutex.unlock(); }
  };
  struct Publication {
    XpUiScene scene{};
    bool ready = true;
  } publication;
  std::mutex modelMutex;
  Renderer renderer{this};
  std::string name = "font";
  XpUiUpdateState model{}, painting{};
  XpUiUpdateFeedback feedback{}, next{};
  XpUiUpdateEffect pending{};
  bool pendingHaptic = false;
  int inputRowCount = 5, inputFamilyCount = 4;
  virtual ~UpdateUiActivity() = default;
  virtual void prepareRows() = 0;
  virtual void prepareText() = 0;
  virtual void releaseText() {}
  virtual int paintedRowCount() const = 0;
  virtual bool row(uint16_t, XpUiSettingsRow&) const = 0;
  virtual void enterGroup(int) = 0;
  bool buildRustScene(XpUiScene& s) {
    {
      ModelLock lock(*this);
      painting = model;
    }
    prepareRows();
    prepareText();
    s.refresh = XP_UI_FAST;
    return true;
  }
  XpUiPaint rustResources() { return {}; }
  Publication* rustPublication() { return &publication; }
  bool rustSceneRejected() const { return false; }
  void requestUpdate() {}
  void render(RenderLock&&);
  bool paintRustScene(const XpUiScene&);
  void rustPublished();
  void rejectRustScene() {
    ModelLock lock(*this);
    publication.ready = false;
    rustSceneFailed();
  }
  void publishRustScene(const XpUiScene&) {
    ModelLock lock(*this);
    rustPublished();
    publication.ready = true;
  }
  void rustSceneFailed();
  XpUiEffect updateRust(const XpUiInput&);
  int selectedIndex() const { return model.list.selected; }
  int paintedSelectedIndex() const { return next.list.selected; }
  uint8_t phase() const {
    ModelLock lock(*const_cast<UpdateUiActivity*>(this));
    return model.phase;
  }
  void closeRouting() {
    ModelLock lock(*this);
    publication.ready = false;
  }
  void notify(XpUiUpdateEvent e) {
    ModelLock lock(*this);
    xp_ui_updates_notify(&model, e, 0, 0);
  }
  void event(XpUiUpdateEvent e) { notify(e); }
  XpUiUpdateEffect input(const XpUiInput& i) {
    ModelLock lock(*this);
    updateRust(i);
    return pending;
  }
};
struct FontDownloadActivity : UpdateUiActivity {
  enum State { WIFI_SELECTION, LOADING_MANIFEST, GROUP_LIST, FAMILY_LIST, DOWNLOADING, COMPLETE, ERROR };
  using StrRef = uint32_t;
  struct ManifestFamily {
    StrRef name = 0, description = 0;
    uint32_t fileStart = 0, fileCount = 0, totalSize = 0, scriptMask = 0;
    bool installed = false, hasUpdate = false;
  };
  std::unique_ptr<char[]> stringArena_;
  std::vector<ManifestFamily> families_;
  std::vector<int> filteredIndices_;
  std::vector<StrRef> scriptGroupLabels_;
  std::vector<std::string> rowLabels_;
  std::vector<fui::ListItem> rowItems_;
  std::atomic<bool> rowsDirty_{true};
  uint8_t cachedPhase = 255;
  std::string statusText;
  const char* str(StrRef r) const { return stringArena_.get() + r; }
  int groupListItemCount() const { return 1 + scriptGroupLabels_.size(); }
  bool hasGroupScreen() const { return !scriptGroupLabels_.empty(); }
  int paintedRowCount() const override { return rowItems_.size(); }
  // DECLARATIONS
};
bool Renderer::drawUiScene(const XpUiScene&, const XpUiPaint*) {
  auto& a = *static_cast<FontDownloadActivity*>(owner);
  if (a.painting.phase == 3) {
    assert(a.rowItems_.size() == 5);
    for (unsigned i = 1; i < 5; ++i) {
      XpUiSettingsRow row{};
      assert(a.row(i, row));
      assert(std::string(row.label) == "Family" + std::to_string(i - 1));
    }
  }
  return true;
}
// METHODS
int main() {
  FontDownloadActivity a;
  xp_ui_updates_init(&a.model, XP_UI_UPDATE_FONTS, 0);
  a.model.phase = 3;
  a.model.has_groups = 1;
  const char names[] = "\0Family0\0Family1\0Family2\0Family3\0Small\0";
  a.stringArena_ = std::make_unique<char[]>(sizeof(names));
  memcpy(a.stringArena_.get(), names, sizeof(names));
  for (int i = 0; i < 4; ++i) {
    FontDownloadActivity::ManifestFamily f;
    f.name = 1 + 8 * i;
    f.scriptMask = i == 0 ? 1 : 2;
    a.families_.push_back(f);
    a.filteredIndices_.push_back(i);
  }
  a.scriptGroupLabels_ = {33};
  std::thread render([&] { a.render({}); });
  rowsGate.wait();
  XpUiInput back{};
  back.released = XP_UI_BACK;
  a.input(back);
  assert(a.model.phase == 2);
  XpUiInput confirm{};
  confirm.released = XP_UI_CONFIRM;
  a.model.groups.selected = 1;
  a.inputRowCount = 2;
  auto e = a.input(confirm);
  assert(e.action == XP_UI_UPDATE_ENTER_GROUP);
  auto enter = std::async(std::launch::async, [&] { a.enterGroup(e.value); });
  // Fixed bridge blocks the resource mutation; the baseline completes it and corrupts the paused frame.
  for (int i = 0;
       i < 500 && !resourceBlocked && enter.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  rowsGate.go();
  panelGate.wait();
  assert(enter.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  enter.get();
  assert(a.filteredIndices_.size() == 1);
  // The panel is still blocked. Back and an old-row tap must stay responsive and cannot activate a group.
  a.input(back);
  assert(a.model.phase == 2);
  a.feedback.rows.first_row = {0, 0, 100, 20};
  a.feedback.rows.count = a.feedback.rows.uniform = 1;
  XpUiInput tap{};
  tap.touch_flags = XP_UI_TOUCH_RELEASED;
  tap.touch_x = 5;
  tap.touch_y = 5;
  assert(a.input(tap).action == XP_UI_UPDATE_NONE);
  panelGate.go();
  render.join();
  pauseRows = false;
  pausePanel = false;
  // One installed family remains after either special row disappears.
  for (bool update : {false, true}) {
    a.families_[0].installed = update;
    a.families_[0].hasUpdate = update;
    a.model.phase = 3;
    a.model.list.selected = 1;
    a.painting = a.model;
    a.rowsDirty_ = true;
    a.prepareRows();
    assert(a.rowItems_.size() == 2);
    assert(std::string(a.rowItems_[0].label).find(update ? "STR_UPDATE_ALL" : "STR_DOWNLOAD_ALL") == 0);
    a.setFamilyStatus(a.families_[0], true, false);
    a.notify(XP_UI_UPDATE_FONT_DONE);
    XpUiInput dismiss{};
    dismiss.pressed = XP_UI_CONFIRM;
    a.input(dismiss);
    assert(a.model.phase == 3);
    a.painting = a.model;
    a.prepareRows();
    assert(a.rowItems_.size() == 1);
    XpUiUpdateLayout layout{};
    layout.progress_height = 24;
    layout.list.list.screen = {0, 0, 528, 792};
    layout.list.list.area = layout.list.list.screen;
    layout.list.list.font = 10;
    layout.list.list.row_height = 30;
    layout.list.header_height = 36;
    layout.list.hints_height = 30;
    XpUiUpdateResources resources{};
    resources.rows.context = &a;
    resources.rows.count = a.paintedRowCount();
    resources.rows.row = [](void* p, uint16_t i, XpUiSettingsRow* out) -> uint8_t {
      return static_cast<FontDownloadActivity*>(p)->row(i, *out);
    };
    resources.rows.width = [](void*, const char* t, int32_t, uint8_t) -> int32_t { return strlen(t) * 10; };
    resources.rows.line = [](void*, int32_t) -> int32_t { return 24; };
    resources.text = [](void*, uint32_t) -> const char* { return ""; };
    XpUiCommand commands[72];
    XpUiBuffer buffer{commands, nullptr, 72, 0};
    XpUiScene scene{};
    assert(xp_ui_updates_view(&a.painting, &layout, &resources, &buffer, &a.next, &scene) == 1);
    assert(a.next.list.selected == 0);
    a.rustPublished();
    assert(a.model.list.selected == 0);
    assert(std::string(a.fontConfirmLabel()) == "STR_DELETE");
    a.inputRowCount = 1;
    XpUiInput confirm{};
    confirm.released = XP_UI_CONFIRM;
    const auto effect = a.input(confirm);
    assert(effect.action == XP_UI_UPDATE_FONT_ACTIVATE && effect.value == 0);
  }
  a.statusText = std::string(240, 'x');
  a.releaseText();
  assert(a.statusText.capacity() < 240);
  puts(
      "real font row rebuild is coherent across Back/group mutation; input proceeds during panel wait; frame text "
      "released");
}
