#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#ifdef BOOK_RELEASE_LOG
#define LOG_DBG(tag, fmt, ...) ((void)0)
#else
#define LOG_DBG(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
#endif
#define LOG_ERR(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
static uint32_t ticks;
static void event(const char* text, int a = 0, int b = 0) { std::printf("%s %d %d\n", text, a, b); }
uint32_t millis() {
  ticks += 250;
  return ticks;
}
enum {
  STR_SAVE_PROGRESS_FAILED,
  STR_BACK,
  STR_INDEX_FAILED,
  STR_BT_BUILD_REFUSED,
  STR_INDEXING,
  STR_EMPTY_CHAPTER,
  STR_OUT_OF_BOUNDS,
  STR_PAGE_LOAD_ERROR,
  STR_BOOKMARK_REMOVED,
  STR_BOOKMARK_ADDED,
  STR_DICT_NO_DICT_SET,
  STR_FONT_DAMAGED
};
const char* tr(int value) {
  static const char* labels[] = {"sync",   "back",       "index-failed",     "radio-refused",  "indexing",   "empty",
                                 "bounds", "load-error", "bookmark-removed", "bookmark-added", "dictionary", "font"};
  return labels[value];
}
struct {
  const char* get(int) const { return "clipping"; }
} I18N;
constexpr int UI_12_FONT_ID = 12;
namespace EpdFontFamily {
enum { BOLD };
}
struct ReaderRenderSpec {
  uint16_t width, height;
};
struct FontCache {
  void releaseSdFontCaches() { event("release-font-cache"); }
};
struct GfxRenderer {
  bool framebuffer = true, ttf = false, fontCache = false, store = true;
  FontCache cache;
  struct FrameBufferLoan {
    GfxRenderer& renderer;
    bool active = true;
    explicit FrameBufferLoan(GfxRenderer& r) : renderer(r) {
      event("loan-start");
      r.framebuffer = false;
    }
    void end() {
      if (active) {
        event("loan-end");
        renderer.framebuffer = true;
        active = false;
      }
    }
    ~FrameBufferLoan() { end(); }
  };
  bool hasFrameBuffer() const { return framebuffer; }
  void clearScreen() { event("clear"); }
  void getOrientedViewableTRBL(int* t, int* r, int* b, int* l) {
    *t = 1;
    *r = 2;
    *b = 3;
    *l = 4;
    event("margins");
  }
  int getScreenWidth() const { return 600; }
  int getScreenHeight() const { return 800; }
  std::vector<int> getTtfFonts() const { return ttf ? std::vector<int>{1} : std::vector<int>{}; }
  FontCache* getFontCacheManager() { return fontCache ? &cache : nullptr; }
  void drawCenteredText(int font, int y, const char* text, bool centered, int style) {
    std::printf("centered %d %d [%s] %d %d\n", font, y, text, centered, style);
  }
  void displayBuffer() { event("display"); }
  bool storeBwBuffer() {
    event("store", store);
    return store;
  }
};
struct {
  void drawPopup(GfxRenderer&, const char* text) { std::printf("popup [%s]\n", text); }
  void drawButtonHints(GfxRenderer&, const char* a, const char* b, const char* c, const char* d) {
    std::printf("hints [%s][%s][%s][%s]\n", a, b, c, d);
  }
} GUI;
struct {
  uint8_t screenMargin = 6;
  ReaderRenderSpec readerRenderSpec(uint16_t w, uint16_t h) {
    event("spec", w, h);
    return {w, h};
  }
} SETTINGS;
struct UITheme {
  uint8_t status = 20, progress = 4;
  struct Metrics {
    uint8_t statusBarVerticalMargin = 5;
  } metrics;
  static UITheme& getInstance() {
    static UITheme value;
    return value;
  }
  uint8_t getStatusBarHeight() const { return status; }
  uint8_t getProgressBarHeight() const { return progress; }
  const Metrics& getMetrics() const { return metrics; }
};
struct {
  unsigned getFreeHeap() const { return 100000; }
  unsigned getMaxAllocHeap() const { return 80000; }
} ESP;
namespace bleturner {
void settleStart() { event("radio-settle"); }
}  // namespace bleturner
namespace ScreenshotUtil {
void takeScreenshot(GfxRenderer&) { event("screenshot"); }
}  // namespace ScreenshotUtil
struct SdCardFont {
  static inline bool notice = false;
  static bool takeCorruptGlyphNotice() {
    const bool result = notice;
    notice = false;
    event("font-notice", result);
    return result;
  }
};
struct Page {
  uint32_t visibleTextOffset = 4321;
  ~Page() { event("page-drop"); }
};
struct Epub {
  int count = 3;
  uint32_t bytes = 120000;
  int getSpineItemsCount() const { return count; }
  struct Item {
    std::string href;
  };
  Item getSpineItem(int i) const {
    event("spine-item", i);
    return {"chapter.xhtml"};
  }
  uint32_t getCumulativeSpineItemSize(int i) const { return bytes * (i + 1); }
};
struct Section {
  struct Config {
    bool cached = true, partial = false, building = false, complete = true;
    bool createOk = true, startOk = true, buildOk = true, loadOk = true, html = true;
    int page = 1;
    uint16_t count = 10, finalCount = 10;
    bool anchor = false, offset = false;
  } config;
  static Config seed;
  int currentPage;
  uint16_t pageCount;
  std::function<void()> popup;
  Section() : config(seed), currentPage(seed.page), pageCount(seed.count) {}
  Section(const std::shared_ptr<Epub>&, int spine, GfxRenderer&) : Section() { event("section-create", spine); }
  ~Section() { event("section-drop"); }
  bool loadSectionFile(const ReaderRenderSpec&) {
    event("cache-load", config.cached);
    return config.cached;
  }
  bool isPartial() const { return config.partial; }
  bool isBuilding() const { return config.building; }
  bool isBuildComplete() const { return config.complete; }
  bool hasHtmlCache() const { return config.html; }
  std::optional<uint16_t> findAnchor(const std::string& value) const {
    std::printf("find-anchor [%s]\n", value.c_str());
    return config.anchor || pageCount >= 6 ? std::optional<uint16_t>(4) : std::nullopt;
  }
  std::optional<uint16_t> getPageForAnchor(const std::string& value) const {
    std::printf("cached-anchor [%s]\n", value.c_str());
    return config.anchor ? std::optional<uint16_t>(4) : std::nullopt;
  }
  bool buildReachedVisibleTextOffset(uint32_t value) const {
    event("reached-offset", value);
    return pageCount >= 6;
  }
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t value) const {
    event("offset-page", value);
    return config.offset ? std::optional<uint16_t>(2) : std::nullopt;
  }
  bool createSectionFile(const ReaderRenderSpec&, std::function<void()> fn) {
    event("create-all", config.createOk);
    fn();
    if (config.createOk) {
      pageCount = config.finalCount;
      config.partial = false;
      config.building = false;
      config.complete = true;
    }
    return config.createOk;
  }
  bool startBuild(const ReaderRenderSpec&, std::function<void()> fn = {}) {
    event("build-start", config.startOk, bool(fn));
    popup = std::move(fn);
    if (popup) popup();
    if (config.startOk) {
      config.building = true;
      config.complete = false;
    }
    return config.startOk;
  }
  bool buildSomeMore(int pages) {
    event("build-more", pages, config.buildOk);
    if (popup) popup();
    if (!config.buildOk) return false;
    pageCount = std::min<int>(config.finalCount, pageCount + 3);
    if (pageCount >= config.finalCount) {
      config.partial = false;
      config.building = false;
      config.complete = true;
    }
    return true;
  }
  std::unique_ptr<Page> loadPage(int page) {
    event("page-load", page, config.loadOk);
    return config.loadOk ? std::make_unique<Page>() : nullptr;
  }
  void abandonBuild() { event("abandon"); }
  void clearCache() { event("clear-cache"); }
  uint16_t estimatedTotalPages() const { return config.finalCount; }
};
Section::Config Section::seed{};
struct EpubReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {}
  };
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section;
  GfxRenderer renderer;
  struct Metadata {
    bool succeeds = true;
    void clear() { event("metadata-clear"); }
    bool assign(const Page&) {
      event("metadata-copy", succeeds);
      return succeeds;
    }
  } currentPageMetadata;
  struct Input {
    struct Labels {
      const char* btn1;
      const char* btn2;
      const char* btn3;
      const char* btn4;
    };
    Labels mapLabels(const char* a, const char* b, const char* c, const char* d) {
      event("labels");
      return {a, b, c, d};
    }
  } mappedInput;
  struct Popup {
    bool active = false;
    bool isActive() const { return active; }
    void render(GfxRenderer&) { event("overlay-popup"); }
  } overlayPopup;
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Overlay overlay = Overlay::None;
  int currentSpineIndex = 1, cachedSpineIndex = 1, cachedChapterTotalPageCount = 0, nextPageNumber = 0;
  int currentPageLinkMarginLeft = 0, currentPageLinkMarginTop = 0;
  int lastSavedSpineIndex = -1, lastSavedPage = -1, lastSavedPageCount = -1;
  std::optional<uint16_t> pendingPageJump;
  std::optional<uint32_t> pendingOffsetJump, cachedVisibleTextOffset, currentPageVisibleOffset;
  std::string pendingAnchor;
  bool partialRebuildStartFailed = true, automaticPageTurnActive = false, pendingSyncSaveError = false;
  bool pendingPercentJump = false, buildPopupPending = false, pageBufferStale = true, overlayPageStored = false;
  bool pendingScreenshot = false, showBookmarkMessage = false, bookmarkRemoved = false, showDictionaryMessage = false;
  bool showFontDamagedMessage = false, showClippingMessage = false, room = true, save = true, toolbar = true;
  float pendingSpineProgress = .75f;
  int clippingMessage = 0;
  uint8_t pageLoadRetryCount = 0;
  uint16_t buildViewportWidth = 0, buildViewportHeight = 0;
  unsigned long lastRenderCompleteMs = 0, fontDamagedMessageTime = 0;
  int pagesUntilFullRefresh = 7;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15, BUILD_PAGES_PER_CHUNK = 8, BUILD_POPUP_PAGE_THRESHOLD = 20;
  static constexpr unsigned BUILD_POPUP_BYTE_THRESHOLD = 96 * 1024, BUILD_POPUP_DEADLINE_MS = 1000;
  static constexpr uint8_t MAX_PAGE_LOAD_RETRIES = 3;
  int& renderRefreshCounter() { return pagesUntilFullRefresh; }
  void settleOverlayRefresh() { event("settle-overlay"); }
  bool makeRoomForChapterBuild() {
    event("build-room", room);
    return room;
  }
  void showBuildPopup(GfxRenderer& r, int& counter) {
    if (!buildPopupPending || !r.hasFrameBuffer()) return;
    GUI.drawPopup(r, tr(STR_INDEXING));
    counter = 1;
    buildPopupPending = false;
  }
  void showBuildPopup() { showBuildPopup(renderer, pagesUntilFullRefresh); }
  void clearDeferredReposition() {
    event("clear-deferred");
    cachedChapterTotalPageCount = 0;
    cachedVisibleTextOffset.reset();
  }
  bool applyDeferredReposition() {
    event("apply-deferred");
    return false;
  }
  void renderStatusBar() { event("status-bar"); }
  void updateBookmarkFlag() { event("bookmark-flag"); }
  void discardOverlayPage() {
    event("discard-overlay");
    overlayPageStored = false;
  }
  void renderContents(std::unique_ptr<Page> page, int t, int r, int b, int l) {
    std::printf("contents %d %d %d %d\n", t, r, b, l);
  }
  void markPageRendered() { event("mark-rendered"); }
  bool saveProgress(int spine, int page, int count) {
    event("save-spine", spine);
    event("save-page", page, count);
    return save;
  }
  bool usesToolbarMenu() const { return toolbar; }
  void renderOverlay() { event("render-overlay"); }
  void pushOverlayRefresh() { event("push-overlay"); }
  void requestUpdate() { event("update"); }
  void renderBook();
  void dump() {
    std::printf("state %d %d %d %d %d %d %d %d %d %d %d %u %u %lu %lu %d %d %d %d %u [%s]\n", currentSpineIndex,
                section ? section->currentPage : -999, section ? section->pageCount : -1, partialRebuildStartFailed,
                pendingPercentJump, buildPopupPending, pageBufferStale, automaticPageTurnActive, pendingSyncSaveError,
                pagesUntilFullRefresh, pageLoadRetryCount, buildViewportWidth, buildViewportHeight,
                lastRenderCompleteMs, fontDamagedMessageTime, lastSavedSpineIndex, lastSavedPage, lastSavedPageCount,
                bool(currentPageVisibleOffset), currentPageVisibleOffset.value_or(0), pendingAnchor.c_str());
    std::printf("flags %d %d %d %d %d %d %d %d %d %d %u\n", pendingScreenshot, showBookmarkMessage,
                showDictionaryMessage, showFontDamagedMessage, showClippingMessage, overlayPageStored,
                currentPageLinkMarginLeft, currentPageLinkMarginTop, bool(pendingPageJump), bool(pendingOffsetJump),
                cachedVisibleTextOffset.value_or(0));
  }
};
// BOOK_METHOD
int main() {
  for (int scenario = 0; scenario < 28; ++scenario)
    for (int variant = 0; variant < 8; ++variant) {
      ticks = 0;
      Section::seed = {};
      SdCardFont::notice = false;
      EpubReaderActivity a;
      auto& cfg = Section::seed;
      bool existing = true;
      a.automaticPageTurnActive = variant & 1;
      UITheme::getInstance().status = variant & 2 ? 0 : (variant & 4 ? 4 : 20);
      a.renderer.ttf = variant & 2;
      a.renderer.fontCache = variant & 4;
      a.pendingSyncSaveError = variant & 4;
      switch (scenario) {
        case 1:
          a.epub.reset();
          break;
        case 2:
          a.currentSpineIndex = -1;
          break;
        case 3:
          a.currentSpineIndex = 7;
          break;
        case 4:
          existing = false;
          break;
        case 5:
          existing = false;
          cfg.cached = false;
          cfg.count = 0;
          cfg.complete = false;
          break;
        case 6:
          existing = false;
          cfg.cached = false;
          a.pendingPercentJump = true;
          break;
        case 7:
          existing = false;
          cfg.cached = false;
          a.pendingPercentJump = true;
          a.room = false;
          break;
        case 8:
          existing = false;
          cfg.cached = false;
          a.pendingPercentJump = true;
          cfg.createOk = false;
          break;
        case 9:
          existing = false;
          cfg.cached = false;
          cfg.startOk = false;
          break;
        case 10:
          existing = false;
          cfg.cached = false;
          cfg.count = 0;
          cfg.buildOk = false;
          a.nextPageNumber = 8;
          break;
        case 11:
          existing = false;
          cfg.partial = true;
          cfg.count = 30;
          cfg.finalCount = 40;
          break;
        case 12:
          existing = false;
          cfg.partial = true;
          cfg.count = 2;
          cfg.complete = false;
          a.nextPageNumber = 8;
          break;
        case 13:
          cfg.partial = true;
          cfg.count = 2;
          cfg.page = 8;
          cfg.complete = false;
          break;
        case 14:
          cfg.building = true;
          cfg.count = 2;
          cfg.page = 8;
          cfg.complete = false;
          break;
        case 15:
          cfg.count = 0;
          cfg.finalCount = 0;
          break;
        case 16:
          cfg.page = -1;
          break;
        case 17:
          cfg.loadOk = false;
          a.pageLoadRetryCount = variant;
          break;
        case 18:
          a.currentPageMetadata.succeeds = false;
          a.pageLoadRetryCount = variant;
          break;
        case 19:
          a.save = false;
          break;
        case 20:
          a.overlay = EpubReaderActivity::Overlay::Text;
          a.overlayPopup.active = true;
          a.pendingScreenshot = true;
          a.showBookmarkMessage = true;
          a.bookmarkRemoved = variant & 2;
          a.showDictionaryMessage = true;
          SdCardFont::notice = true;
          a.showClippingMessage = true;
          break;
        case 21:
          existing = false;
          cfg.offset = true;
          a.pendingOffsetJump = 123;
          break;
        case 22:
          existing = false;
          cfg.anchor = true;
          a.pendingAnchor = "anchor";
          break;
        case 23:
          existing = false;
          a.pendingPercentJump = true;
          a.pendingSpineProgress = 1;
          break;
        case 24:
          cfg.partial = true;
          cfg.count = 2;
          cfg.page = 8;
          cfg.startOk = false;
          cfg.complete = false;
          break;
        case 25:
          cfg.partial = true;
          cfg.count = 2;
          cfg.page = 8;
          a.room = false;
          break;
        case 26:
          existing = false;
          cfg.cached = false;
          cfg.count = 0;
          cfg.complete = false;
          cfg.html = false;
          a.pendingAnchor = "anchor";
          break;
        case 27:
          existing = false;
          cfg.cached = false;
          cfg.count = 0;
          cfg.complete = false;
          cfg.offset = true;
          a.cachedVisibleTextOffset = 123;
          break;
        default:
          break;
      }
      if (existing) a.section = std::make_unique<Section>();
      std::printf("case %d %d\n", scenario, variant);
      a.renderBook();
      a.dump();
    }
}
