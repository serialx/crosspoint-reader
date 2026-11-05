#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#ifdef MENU_RELEASE_LOG
#define LOG_DBG(tag, fmt, ...) ((void)0)
#else
#define LOG_DBG(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
#endif
#define LOG_ERR(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
static void event(const char* name, int a = 0, int b = 0) { std::printf("%s %d %d\n", name, a, b); }
static int lockDepth = 0;
struct RenderLock {
  RenderLock() {
    ++lockDepth;
    event("lock", lockDepth);
  }
  ~RenderLock() {
    event("unlock", lockDepth);
    --lockDepth;
  }
};
namespace HalDisplay {
enum Mode { FAST_REFRESH };
}
struct GfxRenderer {
  void restoreBwBuffer(bool resync) { event("restore", resync, lockDepth); }
  void displayBuffer(HalDisplay::Mode mode) { event("display", mode, lockDepth); }
};
struct MappedInputManager {
  bool touch = false;
  bool hasTouch() const { return touch; }
};
enum StrId {
  STR_PORTRAIT,
  STR_LANDSCAPE_CW,
  STR_ORIENTATION_INVERTED,
  STR_LANDSCAPE_CCW,
  STR_ORIENTATION,
  STR_AUTO_TURN_PAGES_PER_MIN,
  STR_STATE_OFF
};
const char* tr(StrId id) {
  assert(id == STR_STATE_OFF);
  return "Off";
}
struct CrossPointSettings {
  static constexpr int ORIENTATION_COUNT = 4;
  uint8_t orientation = 0, screenInverted = 0, frontlightOn = 0;
  void saveToFile() { event("save-settings", lockDepth); }
} SETTINGS;
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
uint32_t millis() {
  event("clock", lockDepth);
  return 12345;
}
struct {
  bool on = false;
  bool isOn() const {
    event("light-get", on);
    return on;
  }
  void setOn(bool value) {
    on = value;
    event("light-set", value);
  }
} Frontlight;
struct Popup {
  std::function<void(int)> callback;
  void show(StrId title, const StrId* ids, int count, int current, std::function<void(int)> result) {
    event("popup-ids", title, current);
    for (int i = 0; i < count; ++i) event("id", ids[i]);
    callback = std::move(result);
  }
  void show(StrId title, const std::vector<std::string>& labels, int current, std::function<void(int)> result) {
    event("popup-labels", title, current);
    for (const auto& label : labels) std::printf("label [%s]\n", label.c_str());
    callback = std::move(result);
  }
};
struct Epub {
  std::string getTitle() const {
    event("title");
    return "Fixture";
  }
  std::string getPath() const {
    event("path");
    return "/book.epub";
  }
  void clearCache() { event("clear-cache", lockDepth); }
  void setupCacheDir() { event("setup-cache", lockDepth); }
};
struct Section {
  int currentPage = 5;
  uint16_t pageCount = 12;
  std::string text = "Page text";
  ~Section() { event("section-drop", lockDepth); }
  std::string getTextFromSectionFile() const {
    event("page-text", lockDepth);
    return text;
  }
};
struct ChapterPosition {
  int pageIndex = 5, totalPages = 12;
  int displayPage() const { return pageIndex + 1; }
};
struct Metadata {
  std::vector<int> notes;
  const std::vector<int>& footnotes() const {
    event("notes", lockDepth);
    return notes;
  }
};
struct {
  bool clips = false;
  bool hasClippings() const {
    event("clips", clips);
    return clips;
  }
} CLIPPINGS;
struct StripBuffers {
  void releaseIdle() { event("release-strips", lockDepth); }
};
struct Fonts {
  int registryValue = 0;
  int& registry() {
    event("registry");
    return registryValue;
  }
};
static Fonts sdFontSystem;
struct ReaderToolbarUi {
  explicit ReaderToolbarUi(GfxRenderer&) { event("new-toolbar"); }
  void begin() { event("begin-toolbar"); }
};
struct MenuResult {
  uint8_t orientation = 1, pageTurnOption = 3;
  int action = 6;
};
struct ProgressChangeResult {
  int marker = 91;
};
struct ChapterResult {
  int spineIndex = 7;
  std::string anchor = "anchor";
};
struct PercentResult {
  int percent = 37;
};
struct ActivityResult {
  bool isCancelled = false;
  std::variant<MenuResult, ProgressChangeResult, ChapterResult, PercentResult> data;
};
struct Child {
  const char* kind;
  explicit Child(const char* name) : kind(name) { std::printf("new [%s] %d\n", name, lockDepth); }
  virtual ~Child() { std::printf("drop [%s] %d\n", kind, lockDepth); }
};
template <class T>
void arg(const T&) {}
void arg(int value) { event("arg", value); }
void arg(uint8_t value) { event("arg-u8", value); }
void arg(bool value) { event("arg-bool", value); }
void arg(const std::string& value) { std::printf("arg-string [%s]\n", value.c_str()); }
struct EpubReaderMenuActivity : Child {
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    NIGHT_MODE,
    FRONTLIGHT,
    BLUETOOTH,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    SAVE_CLIPPING,
    VIEW_CLIPPINGS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY
  };
  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };
  template <class... A>
  explicit EpubReaderMenuActivity(A&&... args) : Child("menu") {
    (arg(args), ...);
  }
  static void toggleBluetooth() { event("bluetooth"); }
  static void buildMenuItems(std::vector<MenuItem>& items, bool footnotes, bool bookmarks, bool clips) {
    event("build-items", footnotes, bookmarks);
    event("build-clips", clips);
    items.clear();
    for (int action = 0; action < 19; ++action) items.push_back({static_cast<MenuAction>(action), STR_PORTRAIT});
  }
};
#define CHILD(type, name)                      \
  struct type : Child {                        \
    template <class... A>                      \
    explicit type(A&&... args) : Child(name) { \
      (arg(args), ...);                        \
    }                                          \
  }
CHILD(EpubReaderChapterSelectionActivity, "chapter");
struct TextSettingsActivity : Child {
  enum class Tab { Family };
  template <class... A>
  explicit TextSettingsActivity(A&&... args) : Child("text") {
    (arg(args), ...);
  }
};
CHILD(EpubReaderPercentSelectionActivity, "percent");
CHILD(QrDisplayActivity, "qr");
CHILD(EpubReaderBookmarksActivity, "bookmarks");
#undef CHILD
struct EpubReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {}
  };
  enum class Overlay { None, Toolbar, Contents, Text, More };
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section;
  std::unique_ptr<ReaderToolbarUi> toolbarUi;
  StripBuffers stripBuffers;
  Metadata currentPageMetadata;
  Popup overlayPopup;
  std::vector<int> cachedBookmarks;
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;
  std::unique_ptr<Child> child;
  std::function<void(const ActivityResult&)> callback;
  int pendingManualTurn = -1, focusedTool = 2, currentSpineIndex = 3, cachedSpineIndex = -1,
      cachedChapterTotalPageCount = 0, nextPageNumber = 0, autoTurnOption = 2;
  bool panelHoldJumped = true, panelCursorShown = false, toolbar = false, overlayPageStored = false,
       pendingScreenshot = false, showBookmarkMessage = false, saveOk = true;
  uint32_t bookmarkMessageTime = 0;
  Overlay overlay = Overlay::More;
  std::string pendingAnchor;
  template <class T, class F>
  void startActivityForResult(std::unique_ptr<T> value, F result) {
    std::printf("start [%s] %d\n", value->kind, lockDepth);
    child = std::move(value);
    callback = std::move(result);
  }
  bool usesToolbarMenu() const { return toolbar; }
  ChapterPosition chapterPosition() const {
    event("position");
    return {};
  }
  int bookPercentFor(const ChapterPosition&) const {
    event("percent");
    return 42;
  }
  int currentTocIndex() const {
    event("toc", lockDepth);
    return 4;
  }
  void discardOverlayPage() {
    event("discard", overlayPageStored);
    overlayPageStored = false;
  }
  void requestUpdate() { event("update", lockDepth); }
  void applyOrientation(uint8_t orientation) {
    event("orientation", orientation);
    SETTINGS.orientation = orientation;
  }
  void toggleAutoPageTurn(uint8_t option) { event("auto", option); }
  void loadCachedBookmarks() { event("load-bookmarks"); }
  void applyProgressChange(const ProgressChangeResult& result) { event("progress", result.marker); }
  void rememberCurrentContentOffset() { event("remember", lockDepth); }
  void clearDeferredReposition() { event("clear-deferred", lockDepth); }
  void openFootnoteSelect(bool reopen) { event("footnotes", reopen); }
  void jumpToPercent(int percent) { event("jump", percent); }
  void openDictionaryWordSelect() { event("dictionary"); }
  void onGoHome() { event("home", lockDepth); }
  bool saveProgress(int spine, int page, int count) {
    event("save-progress", spine, page);
    event("save-count", count);
    return saveOk;
  }
  bool launchKOReaderSync() {
    event("sync");
    return true;
  }
  void startClipSelection() { event("clip"); }
  void openClippings() { event("clippings"); }
  void addBookmark() { event("bookmark"); }
  void settleOverlayRefresh() { event("settle", lockDepth); }
  void renderOverlay() { event("render-overlay", lockDepth); }
  void pushOverlayRefresh() { event("push-overlay", lockDepth); }
  void paintOverlayPopup() { event("paint-popup", lockDepth); }
  void runMenuPolicy(uint8_t operation, int32_t argument = 0, const ActivityResult* result = nullptr);
  void openReaderMenu();
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction);
  void buildMoreActions();
  void activateMoreRow(int);
};
// MENU_METHODS
int main() {
  using MA = EpubReaderMenuActivity::MenuAction;
  for (int operation = 0; operation < 4; ++operation) {
    for (int action = -1; action < 20; ++action) {
      if ((operation == 0 || operation == 3) && action != 0) continue;
      for (int mask = 0; mask < 128; ++mask) {
        for (int cancelled = 0; cancelled < 2; ++cancelled) {
          event("case", operation, action);
          event("config", mask, cancelled);
          EpubReaderActivity a;
          if (mask & 1) {
            a.section = std::make_unique<Section>();
            a.section->currentPage = mask & 8 ? -1 : 5;
            a.section->text = mask & 16 ? "" : "Page text";
          }
          a.toolbar = mask & 2;
          a.mappedInput.touch = mask & 4;
          a.saveOk = !(mask & 16);
          if (mask & 32) a.currentPageMetadata.notes.push_back(1);
          if (mask & 64) {
            a.cachedBookmarks.push_back(1);
            a.toolbarUi = std::make_unique<ReaderToolbarUi>(a.renderer);
          }
          a.overlayPageStored = mask & 32;
          CLIPPINGS.clips = mask & 64;
          SETTINGS.orientation = mask & 4 ? 1 : 0;
          SETTINGS.screenInverted = mask & 8 ? 1 : 0;
          SETTINGS.frontlightOn = 0;
          Frontlight.on = mask & 8;
          for (int i = 0; i < 19; ++i) a.moreItems.push_back({static_cast<MA>(i), STR_PORTRAIT});
          switch (operation) {
            case 0:
              a.openReaderMenu();
              break;
            case 1:
              a.onReaderMenuConfirm(static_cast<MA>(action));
              break;
            case 2:
              a.activateMoreRow(action);
              break;
            case 3:
              a.buildMoreActions();
              break;
          }
          if (a.callback) {
            const std::string kind = a.child->kind;
            ActivityResult result;
            result.isCancelled = cancelled;
            if (kind == "menu")
              result.data = MenuResult{};
            else if (kind == "chapter")
              result.data = ChapterResult{};
            else if (kind == "percent")
              result.data = PercentResult{};
            else
              result.data = ProgressChangeResult{};
            auto callback = std::move(a.callback);
            a.child.reset();
            event("child-result", cancelled);
            callback(result);
          }
          if (a.overlayPopup.callback) {
            auto callback = std::move(a.overlayPopup.callback);
            a.overlayPopup = Popup{};
            event("popup-result", cancelled);
            callback(cancelled ? 1 : 3);
          }
          assert(lockDepth == 0);
          event("cached", a.cachedSpineIndex, a.cachedChapterTotalPageCount);
          event("page", a.nextPageNumber, bool(a.section));
          event("spine", a.currentSpineIndex);
          std::printf("anchor [%s]\n", a.pendingAnchor.c_str());
          event("overlay", static_cast<int>(a.overlay), a.overlayPageStored);
          event("focus", a.focusedTool, a.panelHoldJumped);
          event("cursor", a.panelCursorShown, a.pendingManualTurn);
          event("messages", a.showBookmarkMessage, a.pendingScreenshot);
          event("bookmark-time", a.bookmarkMessageTime);
          event("settings", SETTINGS.orientation, SETTINGS.screenInverted);
          event("frontlight", SETTINGS.frontlightOn, a.autoTurnOption);
          for (const auto& item : a.moreItems) event("item", static_cast<int>(item.action));
        }
      }
    }
  }
}
