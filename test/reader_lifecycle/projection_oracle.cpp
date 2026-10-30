#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "ChapterPosition.h"
#include "CrossPointPosition.h"
#include "util/ScreenshotInfo.h"
static int lockDepth = 0, modelDepth = 0;
static void event(const char* name, int a = 0, int b = 0) {
  assert(!modelDepth);
  std::printf("%s %d %d\n", name, a, b);
}
static uint32_t bits(float value) {
  uint32_t result;
  std::memcpy(&result, &value, 4);
  return result;
}
static float fromBits(uint32_t value) {
  float result;
  std::memcpy(&result, &value, 4);
  return result;
}
unsigned long millis() {
  event("clock", lockDepth);
  return 1234;
}
struct RenderLock {
  RenderLock() { event("lock", ++lockDepth); }
  ~RenderLock() { event("unlock", lockDepth--); }
};
namespace bleturner {
void acknowledgeLinkNote() { event("ack"); }
}  // namespace bleturner
namespace ReaderUtils {
constexpr unsigned long BOOKMARK_HOLD_MS = 400, GO_HOME_MS = 1000;
}
namespace BoardConfig {
bool touch = false;
bool hasTouch() { return touch; }
}  // namespace BoardConfig
struct CrossPointSettings {
  enum {
    LP_MENU_KOSYNC = 0,
    LP_MENU_DISABLED = 1,
    LP_MENU_BOOKMARK = 2,
    LP_MENU_DICTIONARY = 3,
    LP_MENU_READER_MENU = 4,
    LP_MENU_CREATE_CLIPPING = 5,
    LP_MENU_FOOTNOTES = 6
  };
  int longPressMenuFunction = 0;
} SETTINGS;
struct Store {
  bool credentials = false;
  bool hasCredentials() {
    event("credentials", credentials);
    return credentials;
  }
} KOREADER_STORE;
struct GfxRenderer {};
struct Gui {
  void drawPopup(GfxRenderer&, const char* text) { std::printf("popup [%s]\n", text); }
} GUI;
constexpr int STR_SAVE_PROGRESS_FAILED = 0;
const char* tr(int) { return "Save progress failed"; }
static bool overrideProgress = false;
static uint32_t progressBits = 0;
struct Epub {
  int count = 4, size = 100;
  int getBookSize() const { return size; }
  int getSpineItemsCount() const { return count; }
  std::string getTitle() const {
    event("title");
    return "Original projection title longer than the small-string storage.";
  }
  float calculateProgress(int spine, float fraction) const {
    std::printf("progress %d %08x\n", spine, bits(fraction));
    return overrideProgress ? fromBits(progressBits) : (spine + fraction) / 4.0f;
  }
};
struct Section {
  int currentPage = 0, estimated = 5, toc = 2;
  uint16_t pageCount = 5;
  bool building = false, cached = false;
  std::optional<uint16_t> paragraph = 456;
  std::optional<uint32_t> offset = 0xfedcba98;
  Section() = default;
  Section(const std::shared_ptr<Epub>&, int spine, GfxRenderer&) {
    cached = true;
    event("cached-new", spine);
    toc = -1;
  }
  ~Section() { event(cached ? "cached-drop" : "section-drop", lockDepth); }
  bool isBuilding() const { return building; }
  int estimatedTotalPages() const {
    event("estimate", estimated);
    return estimated;
  }
  int getTocIndexForPage(int page) const {
    event("toc", page, toc);
    return toc;
  }
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const {
    event("paragraph", page, lockDepth);
    return paragraph;
  }
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t page) const {
    event("offset", page, lockDepth);
    return offset;
  }
};
struct ReaderActivity {
  bool baseInput = false;
  bool inputOverPage() const {
    event("base-input", baseInput);
    return baseInput;
  }
};
struct EpubReaderActivity : ReaderActivity {
  struct ModelLock {
    explicit ModelLock(const EpubReaderActivity&) { ++modelDepth; }
    ~ModelLock() { --modelDepth; }
  };
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  mutable GfxRenderer renderer;
  int currentSpineIndex = 0, nextPageNumber = 2, cachedChapterTotalPageCount = 8;
  uint16_t pendingPageJump = 123;
  unsigned long lastPageTurnTime = 56;
  bool automaticPageTurnActive = true, pendingSyncSaveError = true, toolbar = false;
  enum class Overlay { None, Toolbar };
  Overlay overlay = Overlay::None;
  bool usesToolbarMenu() const {
    event("toolbar", toolbar);
    return toolbar;
  }
  void startClipSelection() { event("selection"); }
  void openOverlay(Overlay value) { event("overlay", static_cast<int>(value)); }
  void openReaderMenu() { event("menu"); }
  void clearDeferredReposition() { event("clear", lockDepth); }
  ChapterPosition chapterPosition() const;
  int bookPercentFor(const ChapterPosition&) const;
  int currentTocIndex() const;
  unsigned long confirmLongPressThreshold() const;
  ScreenshotInfo getScreenshotInfo() const;
  int getProgressBasisPoints() const;
  int getProgressPercent() const {
    event("fallback-percent");
    return getScreenshotInfo().progressPercent;
  }
  CrossPointPosition getCurrentPosition() const;
  bool remoteShortcut(bool);
  bool isAtEndOfBook() const;
  void onReturnFromEndOfBook();
  void onEndOfBookRendered();
  bool inputOverPage() const;
  bool pageTurn(bool);
  bool skipPages(int);
  bool applyPageMove(const XpUiReaderMove&, bool);
  uint32_t runProjection(uint8_t, int32_t = 0, int32_t = 0, uint8_t = 0, void* = nullptr) const;
  void dump() const {
    std::printf("state %d %d %d %d %lu %d %d %d\n", currentSpineIndex, nextPageNumber, pendingPageJump,
                section ? section->currentPage : -99, lastPageTurnTime, automaticPageTurnActive, pendingSyncSaveError,
                !!section);
  }
};
// PROJECTION_METHODS
static void screenshot(const ScreenshotInfo& s) {
  std::printf("screenshot %u [%s] %d %d %d %d\n", static_cast<unsigned>(s.readerType), s.title, s.spineIndex,
              s.currentPage, s.totalPages, s.progressPercent);
}
static void position(const CrossPointPosition& p) {
  std::printf("position %d %d %d %08x %d %u %d %u %d [%s] %d %d\n", p.spineIndex, p.pageNumber, p.totalPages,
              p.visibleTextOffset, p.hasVisibleTextOffset, p.paragraphIndex, p.hasParagraphIndex, p.liIndex,
              p.hasLiIndex, p.xpathAnchorId, p.hasResolvedSpineIndex, p.hasMappedPage);
}
static void configure(EpubReaderActivity& o, int flags, int page, int total, int spine) {
  o.section->currentPage = page;
  o.section->estimated = total;
  o.section->building = flags & 4;
  o.section->toc = flags & 8 ? -1 : 2;
  if (flags & 8) o.section->paragraph.reset();
  if (flags & 16) o.section->offset.reset();
  if (flags & 32) o.epub->size = 0;
  if (flags & 1) o.section.reset();
  if (flags & 2) o.epub.reset();
  o.currentSpineIndex = spine;
  o.toolbar = flags & 4;
  o.baseInput = flags & 8;
  o.overlay = flags & 16 ? EpubReaderActivity::Overlay::Toolbar : EpubReaderActivity::Overlay::None;
  o.pendingSyncSaveError = flags & 32;
}
int main() {
  for (int flags = 0; flags < 64; ++flags)
    for (int page : {-1, 0, 1, 4, 5, 65536})
      for (int total : {-1, 0, 1, 5})
        for (int spine : {0, 3, 4}) {
          std::printf("case %d %d %d %d\n", flags, page, total, spine);
          {
            EpubReaderActivity o;
            configure(o, flags, page, total, spine);
            auto p = o.chapterPosition();
            event("chapter", p.pageIndex, p.totalPages);
            event("percent", o.bookPercentFor(p));
            event("toc-result", o.currentTocIndex());
            screenshot(o.getScreenshotInfo());
            event("basis", o.getProgressBasisPoints());
            position(o.getCurrentPosition());
            event("input-over", o.inputOverPage());
            event("at-end", o.isAtEndOfBook());
            o.onReturnFromEndOfBook();
            o.dump();
            o.onEndOfBookRendered();
            o.dump();
          }
          for (int action = 0; action < 6; ++action) {
            EpubReaderActivity o;
            configure(o, flags, page, total, spine);
            event("action", action);
            // The reader requires a book whenever a live section can move into another spine.
            if (o.section && !o.epub) o.section.reset();
            bool result = action < 2   ? o.remoteShortcut(action)
                          : action < 4 ? o.pageTurn(action == 3)
                                       : o.skipPages(action == 4 ? -5 : 5);
            event("result", result);
            o.dump();
          }
          assert(lockDepth == 0 && modelDepth == 0);
        }
  for (int setting = -1; setting <= 8; ++setting)
    for (bool touch : {false, true})
      for (bool credentials : {false, true}) {
        SETTINGS.longPressMenuFunction = setting;
        BoardConfig::touch = touch;
        KOREADER_STORE.credentials = credentials;
        EpubReaderActivity o;
        event("threshold-case", setting, touch);
        event("threshold", o.confirmLongPressThreshold());
      }
  overrideProgress = true;
  for (uint32_t value : {0xbf800000u, 0x80000000u, 0u, 0x3f800000u, 0x40000000u, 0x3f7fffffu, 0x3f800001u}) {
    progressBits = value;
    std::printf("float %08x\n", value);
    EpubReaderActivity o;
    event("percent", o.bookPercentFor({2, 5}));
    screenshot(o.getScreenshotInfo());
    event("basis", o.getProgressBasisPoints());
  }
}
