#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "util/HomeButtonInput.h"
static uint32_t clockMs = 2000;
static bool lockAvailable = true, busy = false;
static unsigned logIndex = 0;
static unsigned lockDepth = 0, lockInstances = 0;
static void event(const char* name, int a = 0, int b = 0) { std::printf("%u %s %d %d\n", logIndex++, name, a, b); }
static unsigned long millis() {
  event("millis", clockMs);
  return clockMs;
}
template <typename... Args>
static void debug(Args&&...) {
  event("debug");
}
template <typename... Args>
static void error(Args&&...) {
  event("error");
}
#ifndef LOOP_LOG_LEVEL
#define LOOP_LOG_LEVEL 2
#endif
#if LOOP_LOG_LEVEL >= 2
#define LOG_DBG(...) debug(__VA_ARGS__)
#else
#define LOG_DBG(...) ((void)0)
#endif
#define LOG_ERR(...) error(__VA_ARGS__)
using ReaderRenderSpec = int;
struct CrossPointSettings {
  enum SHORT_PWRBTN { FOOTNOTES = 4, CREATE_CLIPPING = 6 };
  enum {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LP_MENU_READER_MENU = 4,
    LP_MENU_CREATE_CLIPPING = 5,
    LP_MENU_FOOTNOTES = 6
  };
  enum { CHAPTER_SKIP = 1, ORIENTATION_CHANGE = 2, ORIENTATION_COUNT = 4 };
  uint8_t orientation = 0, longPressMenuFunction = 1, shortPwrBtn = 0, longPressButtonBehavior = 0;
  bool removeReadBooksFromRecents = false, moveFinishedToReadFolder = false, touchReaderControls = false;
  int getReaderFontId() const {
    event("font-id");
    return 1;
  }
  int readerRenderSpec(int w, int h) const {
    event("spec", w, h);
    return 1;
  }
} SETTINGS;
struct MappedInputManager {
  enum class Button { Back, Confirm, Power, Down };
  unsigned released = 0, pressed = 0, suppressed = 0, fired = 0;
  unsigned long held = 0;
  bool longTouch = false, touch = false, tap = false;
  HomeButtonAction home = HomeButtonAction::Ignore;
  bool wasReleased(Button b) const {
    bool v = (released & ~suppressed) & (1u << int(b));
    event("release", int(b), v);
    return v;
  }
  bool wasLongPressed(Button b, unsigned long threshold) {
    bool v = (pressed & (1u << int(b))) && !(fired & (1u << int(b))) && getHeldTime() >= threshold;
    if (v) {
      fired |= 1u << int(b);
      suppressed |= 1u << int(b);
    }
    event("long", int(b), v);
    return v;
  }
  unsigned long getHeldTime() const {
    event("held", held);
    return held;
  }
  bool wasScreenLongPress(int& x, int& y) const {
    event("touch-long", longTouch);
    x = 23;
    y = 45;
    return longTouch;
  }
  bool wasScreenTapped(int& x, int& y) const {
    event("tap", tap);
    x = 12;
    y = 34;
    return tap;
  }
  bool hasTouch() const {
    event("touch", touch);
    return touch;
  }
  HomeButtonAction homeButtonAction() const {
    event("home", int(home));
    return home;
  }
};
struct RenderLock {
  enum class Mode { Try };
  bool owns;
  RenderLock() : owns(true) {
    ++lockDepth;
    ++lockInstances;
    event("lock");
  }
  explicit RenderLock(Mode) : owns(lockAvailable) {
    ++lockInstances;
    if (owns) ++lockDepth;
    event("try", owns);
  }
  ~RenderLock() {
    assert(lockInstances);
    --lockInstances;
    if (owns) {
      assert(lockDepth);
      --lockDepth;
    }
    event("unlock", owns);
  }
  bool ownsLock() const { return owns; }
  static bool peek() {
    event("peek", busy);
    return busy;
  }
};
namespace bleturner {
static bool heap = false;
static int note = 0;
bool holdsHeap() {
  event("radio-heap", heap);
  return heap;
}
int linkNote() {
  event("radio-note", note);
  return note;
}
void afterPaint() { event("radio-after"); }
}  // namespace bleturner
struct {
  unsigned free = 60000, max = 30000;
  unsigned getFreeHeap() const {
    event("heap-free", free);
    return free;
  }
  unsigned getMaxAllocHeap() const {
    event("heap-max", max);
    return max;
  }
} ESP;
struct FontCache {
  struct Scope {
    void endScanAndPrewarm() { event("prewarm-end"); }
  };
  Scope createPrewarmScope() {
    event("prewarm-start");
    return {};
  }
};
struct GfxRenderer {
  bool frame = true, font = true;
  FontCache cache;
  bool hasFrameBuffer() const {
    event("framebuffer", frame);
    return frame;
  }
  FontCache* getFontCacheManager() {
    event("font-cache", font);
    return font ? &cache : nullptr;
  }
};
struct Page {
  void render(GfxRenderer&, int, int, int) { event("page-render"); }
};
struct Section {
  bool building = false, partial = false, startOk = true, pageOk = true;
  int currentPage = 1, pageCount = 30;
  bool isBuilding() const {
    event("building", building);
    return building;
  }
  bool isPartial() const {
    event("partial", partial);
    return partial;
  }
  std::unique_ptr<Page> loadPage(int p) const {
    event("page-load", p, pageOk);
    return pageOk ? std::make_unique<Page>() : nullptr;
  }
  bool startBuild(int) {
    event("build-start", startOk);
    return startOk;
  }
};
struct Epub {
  int count = 5;
  int getSpineItemsCount() const {
    event("spines", count);
    return count;
  }
  std::string getPath() const { return "book"; }
  std::string getTitle() const { return "title"; }
  std::string getAuthor() const { return "author"; }
  std::string getThumbBmpPath() const { return "thumb"; }
  std::string getLanguage() const { return "he"; }
};
static bool inRead = false;
static bool isInReadFolder(const std::string&) {
  event("read-folder", inRead);
  return inRead;
}
struct {
  bool removed = true;
  bool removeByPath(const std::string&) {
    event("recent-remove", removed);
    return removed;
  }
  void addBook(const std::string&, const std::string&, const std::string&, const std::string&) { event("recent-add"); }
} RECENT_BOOKS;
namespace haptic_feedback {
void touchAction(bool longPress = false) { event("haptic", longPress); }
}  // namespace haptic_feedback
namespace ReaderUtils {
struct Touch {
  bool prev = false, next = false;
  unsigned long heldMs = 0;
};
static Touch touch;
static bool prev = false, next = false, tilt = false, menu = false;
bool isRtlBookLanguage(const std::string&) {
  event("rtl");
  return true;
}
Touch detectTouchPageTurn(GfxRenderer&, MappedInputManager&, bool) {
  event("touch-turn", touch.prev, touch.next);
  return touch;
}
std::tuple<bool, bool, bool> detectPageTurn(MappedInputManager&) {
  event("buttons", prev, next);
  return {prev, next, tilt};
}
bool isTouchMenuGesture(GfxRenderer&, MappedInputManager&) {
  event("menu-gesture", menu);
  return menu;
}
}  // namespace ReaderUtils
struct Link {
  std::string href = "target";
};
static bool linkMatch = false;
namespace EpubReaderUtils {
const Link* linkAtPoint(int, int, int, int, int) {
  event("link-hit", linkMatch);
  static Link link;
  return linkMatch ? &link : nullptr;
}
}  // namespace EpubReaderUtils
struct Metadata {
  int links() const { return 0; }
};
struct Popup {
  bool active = false;
  bool isActive() const {
    event("popup", active);
    return active;
  }
  template <class F>
  void handleInput(MappedInputManager&, F f) {
    event("popup-input");
    f();
  }
};
struct EpubReaderActivity {
  enum class Overlay { None, Toolbar, Contents, Text, More };
  Popup loadFailurePopup;
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  MappedInputManager mappedInput;
  GfxRenderer renderer;
  std::atomic<bool> radioReleasedForBuild = false;
  struct {
    bool dirty = false;
    bool repaint(int n) {
      event("note-repaint", n, dirty);
      return dirty;
    }
  } linkNoteTitle;
  uint8_t appliedOrientation = 0;
  unsigned long lastRenderCompleteMs = 100, lastPageTurnTime = 100, pageTurnDuration = 1000;
  static constexpr unsigned RENDER_MIN_FREE_HEAP = 24576, BACKGROUND_BUILD_MIN_MAX_ALLOC = 16384;
  static constexpr int PARTIAL_REBUILD_START_MARGIN = 15;
  int idlePrewarmSpine = -1, idlePrewarmPage = -1, currentSpineIndex = 1, buildViewportWidth = 90,
      buildViewportHeight = 120;
  bool partialRebuildStartFailed = false, recentsEntryRemoved = false, pendingReadFolderMove = false;
  bool showBookmarkMessage = false, showDictionaryMessage = false, showFontDamagedMessage = false,
       showClippingMessage = false;
  unsigned long bookmarkMessageTime = 0, dictionaryMessageTime = 0, fontDamagedMessageTime = 0, clippingMessageTime = 0;
  Overlay overlay = Overlay::None;
  bool automaticPageTurnActive = false;
  int8_t pendingManualTurn = 0;
  bool pendingManualTurnTouch = false;
  int footnoteDepth = 0, currentPageLinkMarginLeft = 5, currentPageLinkMarginTop = 10;
  Metadata currentPageMetadata;
  bool toolbar = true, menuHandled = false, menuActive = false, backHandled = false, endTurnHandled = false,
       turnChanged = true, syncStarted = true;
  unsigned long confirmThreshold = 700;
  void loop();
  void requestUpdate() { event("update"); }
  void finish() { event("finish"); }
  void rememberBookOnceRendered() { event("remember"); }
  void applyOrientation(uint8_t o) {
    event("orientation", o);
    appliedOrientation = o;
  }
  void clearEndOfBookOptionsIfNeeded() { event("end-clear"); }
  bool usesToolbarMenu() {
    event("toolbar", toolbar);
    return toolbar;
  }
  void handleOverlayInput() { event("overlay-input"); }
  void discardOverlayPage() { event("overlay-discard"); }
  void startClipSelection(int x = -1, int y = -1) { event("clip", x, y); }
  bool pageTurn(bool f) {
    event("turn", f, turnChanged);
    return turnChanged;
  }
  void notePageTurn(bool f, bool s) { event("note-turn", f, s); }
  bool handleEndOfBookMenu() {
    event("end-menu", menuHandled);
    return menuHandled;
  }
  bool endOfBookMenuActive() {
    event("end-active", menuActive);
    return menuActive;
  }
  unsigned long confirmLongPressThreshold() {
    event("threshold", confirmThreshold);
    return confirmThreshold;
  }
  void addBookmark() { event("bookmark"); }
  bool launchKOReaderSync() {
    event("sync", syncStarted);
    return syncStarted;
  }
  void openDictionaryWordSelect() { event("dictionary"); }
  void restoreSavedPosition() { event("restore"); }
  void openFootnoteSelect(bool r) { event("footnote", r); }
  void openOverlay(Overlay o) {
    event("overlay-open", int(o));
    overlay = o;
  }
  void openReaderMenu() { event("reader-menu"); }
  void navigateToHref(const std::string&, bool save) { event("href", save); }
  bool handleBackNavigation() {
    event("back", backHandled);
    return backHandled;
  }
  void backgroundBuildTick() { event("build-tick"); }
  bool handleEndOfBookPageTurn(bool p, bool n) {
    event("end-turn", p, n);
    return endTurnHandled;
  }
  bool skipPages(int n) {
    event("skip", n, turnChanged);
    return turnChanged;
  }
};
#include "loop_methods.inc"
static void state(const EpubReaderActivity& r) {
  std::printf("state %d %d %d %d %d %d %d %lu %d %d %d %d %d %d %d %d %d %u %u\n", r.radioReleasedForBuild.load(),
              r.idlePrewarmSpine, r.idlePrewarmPage, r.partialRebuildStartFailed, r.recentsEntryRemoved,
              r.pendingReadFolderMove, int(r.overlay), r.lastPageTurnTime, r.automaticPageTurnActive,
              r.pendingManualTurn, r.pendingManualTurnTouch, r.showBookmarkMessage, r.showDictionaryMessage,
              r.showFontDamagedMessage, r.showClippingMessage, r.appliedOrientation, r.currentSpineIndex,
              r.mappedInput.fired, r.mappedInput.suppressed);
}
int main() {
  for (unsigned phase = 0; phase < 32; ++phase)
    for (unsigned action = 0; action < 36; ++action)
      for (unsigned edge = 0; edge < 4; ++edge) {
        EpubReaderActivity r;
        SETTINGS = {};
        ReaderUtils::touch = {};
        ReaderUtils::prev = false;
        ReaderUtils::next = false;
        ReaderUtils::tilt = false;
        ReaderUtils::menu = false;
        ESP = {};
        RECENT_BOOKS.removed = true;
        bleturner::heap = false;
        bleturner::note = 0;
        inRead = false;
        linkMatch = false;
        lockAvailable = true;
        busy = false;
        clockMs = 2000;
        logIndex = 0;
        r.mappedInput.held = (edge == 0 ? 699 : edge == 1 ? 700 : edge == 2 ? 1000 : 1500);
        ReaderUtils::touch.heldMs = r.mappedInput.held;
        switch (phase) {
          case 1:
            r.loadFailurePopup.active = true;
            break;
          case 2:
            r.epub.reset();
            break;
          case 3:
            r.radioReleasedForBuild = true;
            break;
          case 4:
            r.radioReleasedForBuild = true;
            r.section->building = true;
            break;
          case 5:
            lockAvailable = false;
            r.radioReleasedForBuild = true;
            break;
          case 6:
            r.linkNoteTitle.dirty = true;
            break;
          case 7:
            SETTINGS.orientation = 3;
            break;
          case 8:
            r.lastRenderCompleteMs = 1600;
            break;
          case 9:
            ESP.free = 24000;
            break;
          case 10:
            ESP.max = 16000;
            break;
          case 11:
            r.idlePrewarmSpine = 1;
            r.idlePrewarmPage = 1;
            break;
          case 12:
            r.section->partial = true;
            r.section->currentPage = 20;
            break;
          case 13:
            r.section->partial = true;
            r.section->currentPage = 20;
            r.section->startOk = false;
            break;
          case 14:
            r.currentSpineIndex = 5;
            SETTINGS.removeReadBooksFromRecents = true;
            SETTINGS.moveFinishedToReadFolder = true;
            break;
          case 15:
            r.recentsEntryRemoved = true;
            SETTINGS.removeReadBooksFromRecents = true;
            break;
          case 16:
            r.overlay = EpubReaderActivity::Overlay::More;
            break;
          case 17:
            r.overlay = EpubReaderActivity::Overlay::Text;
            r.toolbar = false;
            break;
          case 18:
            r.automaticPageTurnActive = true;
            break;
          case 19:
            r.automaticPageTurnActive = true;
            busy = true;
            break;
          case 20:
            r.section.reset();
            break;
          case 21:
            r.menuActive = true;
            break;
          case 22:
            r.menuHandled = true;
            break;
          case 23:
            r.footnoteDepth = 2;
            break;
          case 24:
            r.pendingManualTurn = -1;
            r.pendingManualTurnTouch = true;
            break;
          case 25:
            r.lastPageTurnTime = 1900;
            break;
          case 26:
            clockMs = edge == 0 ? 2499 : edge == 1 ? 2500 : edge == 2 ? 2501 : 2999;
            r.showBookmarkMessage = r.showDictionaryMessage = r.showFontDamagedMessage = r.showClippingMessage = true;
            break;
          case 27:
            r.turnChanged = false;
            break;
          case 28:
            r.syncStarted = false;
            break;
          case 29:
            r.backHandled = true;
            break;
          case 30:
            r.endTurnHandled = true;
            break;
          case 31:
            clockMs = 2999;
            r.lastPageTurnTime = 2800;
            r.lastRenderCompleteMs = 0;
            break;
        }
        if (action < 12)
          r.mappedInput.home = static_cast<HomeButtonAction>(action);
        else if (action < 19) {
          SETTINGS.longPressMenuFunction = action - 12;
          r.mappedInput.pressed = 2;
          r.mappedInput.released = 2;
        } else
          switch (action) {
            case 19:
              r.mappedInput.released = 2;
              break;
            case 20:
              r.mappedInput.released = 1;
              break;
            case 21:
              ReaderUtils::next = true;
              break;
            case 22:
              ReaderUtils::prev = true;
              break;
            case 23:
              ReaderUtils::prev = ReaderUtils::next = true;
              break;
            case 24:
              ReaderUtils::next = true;
              ReaderUtils::tilt = true;
              break;
            case 25:
              ReaderUtils::next = true;
              SETTINGS.longPressButtonBehavior = 1;
              break;
            case 26:
              clockMs = edge == 0 ? 2499 : edge == 1 ? 2500 : edge == 2 ? 2501 : 2999;
              ReaderUtils::next = true;
              SETTINGS.longPressButtonBehavior = 2;
              break;
            case 27:
              ReaderUtils::prev = true;
              SETTINGS.longPressButtonBehavior = 2;
              break;
            case 28:
              ReaderUtils::touch.next = true;
              break;
            case 29:
              ReaderUtils::menu = true;
              break;
            case 30:
              SETTINGS.touchReaderControls = true;
              r.mappedInput.longTouch = true;
              break;
            case 31:
              SETTINGS.touchReaderControls = true;
              r.mappedInput.touch = r.mappedInput.tap = true;
              linkMatch = true;
              break;
            case 32:
              SETTINGS.shortPwrBtn = 6;
              r.mappedInput.released = 4;
              break;
            case 33:
              SETTINGS.shortPwrBtn = 4;
              r.mappedInput.released = 4;
              break;
            case 34:
              SETTINGS.shortPwrBtn = 6;
              r.mappedInput.released = 12;
              ReaderUtils::next = true;
              break;
            case 35:
              SETTINGS.shortPwrBtn = 4;
              r.mappedInput.released = 12;
              break;
          }
        std::printf("case %u %u %u\n", phase, action, edge);
        r.loop();
        assert(lockDepth == 0 && lockInstances == 0);
        state(r);
        clockMs += 201;
        r.loop();
        assert(lockDepth == 0 && lockInstances == 0);
        state(r);
      }
}
