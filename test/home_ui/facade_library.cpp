#include <xp_ui_home.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>
using StaticSemaphore_t = std::mutex;
using SemaphoreHandle_t = std::mutex*;
constexpr int portMAX_DELAY = -1;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* p) { return p; }
SemaphoreHandle_t watchedMutex = nullptr;
std::atomic<bool> waitingForResource{false};
void xSemaphoreTake(SemaphoreHandle_t p, int) {
  if (p == watchedMutex) waitingForResource = true;
  p->lock();
}
void xSemaphoreGive(SemaphoreHandle_t p) { p->unlock(); }
constexpr int SMALL_FONT_ID = 1;
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
constexpr int STR_LIBRARY_SEARCH = 1, STR_LIBRARY_TITLE_UNSORTED = 2, STR_LIBRARY = 3;
const char* tr(int i) { return i == 3 ? "Library" : "Title"; }
namespace haptic_feedback {
void touchAction(bool) {}
}  // namespace haptic_feedback
struct FakeRect {
  int16_t x = 0, y = 0, width = 500, height = 700;
};
struct Target {
  Target& deviceContext() { return *this; }
  FakeRect safeRect() { return {}; }
};
struct Theme {
  int tabBarHeight = 24, tabSpacing = 4, contentSidePadding = 12, headerUnderlineSize = 1, verticalSpacing = 4;
  int buttonHintsHeight = 20;
  bool tabPillFullSlot = false;
  uint8_t listRowRadius = 4;
  struct {
    bool bold = false;
  } bodyText;
};
struct UITheme {
  static UITheme& getInstance() {
    static UITheme v;
    return v;
  }
  Theme getMetrics() { return {}; }
  static FakeRect getContentArea(int) { return {}; }
};
struct ThemeCell {
  const Theme* load(std::memory_order) {
    static Theme t;
    return &t;
  }
};
ThemeCell sharedUiThemeCell() { return {}; }
void sampleReaderChromeTheme(int, XpUiReaderTheme&) {}
int bottomCornerClearance() { return 0; }
struct Input {
  bool hasTouch() const { return true; }
};
struct KeyboardResult {
  std::string text;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<KeyboardResult> data;
};
enum class InputType { Text };
struct KeyboardEntryActivity {
  KeyboardEntryActivity(int, Input&, const char*, const std::string&, int, InputType) {}
};
template <class T, class... A>
std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
  return std::make_unique<T>(std::forward<A>(a)...);
}
struct Commands {
  bool ready() const { return true; }
  bool reserve(int) { return true; }
  bool grow() { return false; }
  XpUiBuffer buffer() const { return {}; }
};
namespace library {
enum class SortOrder : uint8_t {};
struct ClixRecord {
  char fold[96]{};
  uint8_t foldLen = 0;
};
std::string fold(const std::string& value) { return value; }
bool matchesQuery(std::string_view, const std::string&) { return true; }
}  // namespace library
struct Index {
  uint16_t bookCount() const { return 4; }
  uint16_t ordinalForRow(library::SortOrder, uint16_t row) { return row; }
  bool readRecord(uint16_t, library::ClixRecord&) { return true; }
  bool readAuthor(const library::ClixRecord&, std::string&) { return true; }
};
struct DummyBook {
  std::string path, title;
};
struct {
  const std::vector<DummyBook>& getBooks() {
    static std::vector<DummyBook> books;
    return books;
  }
} RECENT_BOOKS;
std::mutex renderMutex;
class LibraryListActivity {
 public:
  // RESOURCE_LOCK
  struct ModelLock {
    std::unique_lock<std::mutex> lock;
    explicit ModelLock(LibraryListActivity& owner) : lock(owner.modelMutex) {}
  };
  struct RenderLock {
    std::unique_lock<std::mutex> lock;
    explicit RenderLock(LibraryListActivity&) : lock(renderMutex) {}
  };
  std::mutex modelMutex;
  std::atomic<bool> sceneFailed{false};
  bool rustSceneRejected() const { return sceneFailed; }
  XpUiLibraryState state{};
  bool resourcesChanged = true;
  XpUiHomeListHits publishedHits{};
  struct Frame {
    XpUiLibraryState snapshot{};
    Commands commands;
    XpUiLibraryFeedback next{};
    XpUiHomeListHits hits{};
    XpUiReaderControl headerControls[3]{};
    int headerCount = 0;
    std::string headerSearchTitle;
  } frame;
  Target textTarget;
  Input mappedInput;
  int renderer = 0;
  std::string name = "Library", query;
  Index index;
  bool filterFailed = false;
  std::unique_ptr<uint16_t[]> filtered;
  uint16_t filteredCount = 4, groupCount = 2;
  std::vector<uint16_t> groupStarts{0, 2};
  std::vector<int> winItems;
  std::vector<std::string> winTitles;
  int windowStart = 0;
  bool nativeLongTouch = false;
  uint8_t touchFeedback = 0;
  std::function<void(const ActivityResult&)> searchResult;
  LibraryListActivity() { xp_ui_library_init(&state); }
  void requestUpdate() {}
  void event(uint8_t, int32_t = 0, uint16_t = 0);
  bool buildRustScene(XpUiScene&);
  void rustPublished();
  void applyRustEffect(const XpUiEffect&);
  void openSearch();
  bool collapseGroups(int);
  void expandGroup(int);
  const char* headerTitle() const;
  int listCount() const { return filteredCount; }
  int pinnedCount() const { return 0; }
  void openSelectedBook() {}
  void onGoHome() {}
  void showRecentBookOptions(int) {}
  void promptDeleteBookByPath(const std::string&, const std::string&) {}
  void promptDeleteBook(int) {}
  void restoreExpandedList() { event(XP_UI_LIBRARY_EXPANDED, -1); }
  void promptRebuildIndex() {}
  void swallowHeldReleases() {}
  void startActivityForResult(std::unique_ptr<KeyboardEntryActivity>, std::function<void(const ActivityResult&)> cb) {
    searchResult = std::move(cb);
  }
  bool groupable() const { return true; }
  bool buildGroupStarts() {
    groupStarts = {0, 2};
    return true;
  }
  int groupForBook(int i) { return i / 2; }
  void refreshOverlap() {}
  void applyFilter();
  XpUiLibraryFacts facts(const XpUiLibraryState*) const {
    XpUiLibraryFacts f{};
    f.count = f.books = filteredCount;
    return f;
  }
  XpUiHomeRows rows() {
    XpUiHomeRows r{};
    r.context = this;
    r.window = [](void* p, uint16_t, uint16_t) -> uint8_t {
      auto& self = *static_cast<LibraryListActivity*>(p);
      self.winItems = {1};
      self.winTitles = {self.query + std::to_string(self.filtered ? self.filtered[0] : 0)};
      return 1;
    };
    return r;
  }
};
// Exercise the production scene-construction scope; geometry is covered by the
// differential oracles. This leaf pauses while index/filter resources are borrowed.
std::atomic<bool> building{false}, finishBuild{true}, failBuild{false};
uint8_t test_library_view(const XpUiLibraryState*, const XpUiLibraryFacts*, const XpUiLibraryLayout*,
                          const XpUiHomeRows* rows, const XpUiBuffer*, XpUiLibraryFeedback*, XpUiHomeListHits*,
                          XpUiScene*) {
  building = true;
  while (!finishBuild) std::this_thread::yield();
  rows->window(rows->context, 0, 1);
  return failBuild ? 0 : XP_UI_HOME_VIEW_OK;
}
#define xp_ui_library_view test_library_view
