#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
static void event(const char* name, int a = 0, int b = 0) { std::printf("%s %d %d\n", name, a, b); }
enum StrId {
  STR_FONT_SIZE,
  STR_LINE_SPACING,
  STR_PARA_ALIGNMENT,
  STR_TIGHT,
  STR_NORMAL,
  STR_WIDE,
  STR_EXTRA_WIDE,
  STR_JUSTIFY,
  STR_ALIGN_LEFT,
  STR_CENTER,
  STR_ALIGN_RIGHT,
  STR_BOOK_S_STYLE
};
constexpr StrId kSpacingIds[] = {STR_TIGHT, STR_NORMAL, STR_WIDE, STR_EXTRA_WIDE};
constexpr StrId kAlignIds[] = {STR_JUSTIFY, STR_ALIGN_LEFT, STR_CENTER, STR_ALIGN_RIGHT, STR_BOOK_S_STYLE};
constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
static int lockDepth = 0;
struct RenderLock {
  RenderLock() {
    ++lockDepth;
    event("lock", lockDepth);
  }
  template <class T>
  explicit RenderLock(T&) {
    ++lockDepth;
    event("owner-lock", lockDepth);
  }
  ~RenderLock() {
    event("unlock", lockDepth);
    --lockDepth;
  }
};
struct Renderer {};
struct CrossPointSettings {
  static constexpr int LINE_COMPRESSION_COUNT = 4, PARAGRAPH_ALIGNMENT_COUNT = 5;
  uint8_t orientation = 0, fontPointSize = 14, lineSpacing = 7, paragraphAlignment = 8;
  char sdFontFamilyName[12] = "Fixture";
  void saveToFile() { event("save", lockDepth); }
} SETTINGS;
static uint32_t clockMs = 123456;
uint32_t millis() {
  event("clock", clockMs, lockDepth);
  return clockMs;
}
struct UITheme {
  uint8_t status = 20, progress = 4;
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  uint8_t getStatusBarHeight() const {
    event("status-height", status);
    return status;
  }
  uint8_t getProgressBarHeight() const {
    event("progress-height", progress);
    return progress;
  }
};
namespace ReaderUtils {
void applyOrientation(Renderer&, uint8_t orientation) { event("orientation", orientation, lockDepth); }
}  // namespace ReaderUtils
struct ReaderActivity {
  void applyInitialOrientation() { event("initial"); }
};
struct Fonts {
  int value = 0;
  int& registry() {
    event("registry");
    return value;
  }
  void ensureLoaded(Renderer&) { event("load-font", lockDepth); }
};
static std::vector<uint8_t> sizes;
std::vector<uint8_t> readerFontPointSizes(int*, const char* family) {
  std::printf("sizes [%s]\n", family);
  return sizes;
}
uint8_t snapToNearestPointSize(const std::vector<uint8_t>& values, uint8_t current) {
  event("snap", current);
  assert(!values.empty());
  return values[values.size() / 2];
}
struct Popup {
  std::function<void(int)> callback;
  void show(StrId title, const std::vector<std::string>& labels, int current, std::function<void(int)> result) {
    event("popup-labels", title, current);
    for (const auto& label : labels) std::printf("label [%s]\n", label.c_str());
    callback = std::move(result);
  }
  void show(StrId title, const StrId* ids, int count, int current, std::function<void(int)> result) {
    event("popup-ids", title, current);
    for (int i = 0; i < count; ++i) event("id", ids[i]);
    callback = std::move(result);
  }
};
struct Section {
  int currentPage = 7;
  uint16_t pageCount = 12;
  ~Section() { event("section-drop", lockDepth); }
};
struct EpubReaderActivity : ReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {}
  };
  Renderer renderer;
  Fonts sdFontSystem;
  Popup overlayPopup;
  std::unique_ptr<Section> section;
  int appliedOrientation = 0, currentSpineIndex = 3, cachedSpineIndex = -1, cachedChapterTotalPageCount = 0,
      nextPageNumber = 0;
  bool automaticPageTurnActive = false;
  uint32_t lastPageTurnTime = 89, pageTurnDuration = 321;
  void rememberCurrentContentOffset() { event("remember", lockDepth); }
  void discardOverlayPage() { event("discard", lockDepth); }
  void requestUpdate() { event("update", lockDepth); }
  void paintOverlayPopup() { event("paint-popup", lockDepth); }
  void applyInitialOrientation();
  void applyOrientation(uint8_t);
  void toggleAutoPageTurn(uint8_t);
  void applyReaderTextSettings();
  void applyTextSettingLive();
  void showTextRowPopup(int);
};
// SETTINGS_METHODS
int main() {
  for (int operation = 0; operation < 6; ++operation) {
    for (int argument : {-1, 0, 1, 2, 3, 4, 5, 255}) {
      for (int mask = 0; mask < 32; ++mask) {
        for (int result : {-1, 0, 2, 256}) {
          if (operation != 5 && result != 0) continue;
          event("case", operation, argument);
          event("config", mask, result);
          EpubReaderActivity a;
          if (mask & 1) a.section = std::make_unique<Section>();
          a.appliedOrientation = mask & 2 ? 1 : 0;
          SETTINGS.orientation = mask & 4 ? 1 : 0;
          SETTINGS.fontPointSize = 14;
          SETTINGS.lineSpacing = 7;
          SETTINGS.paragraphAlignment = 8;
          a.automaticPageTurnActive = mask & 8;
          UITheme::getInstance().status = mask & 16 ? 4 : (mask & 8 ? 0 : 20);
          clockMs = mask & 2 ? UINT32_MAX - 10 : 123456;
          sizes = mask & 16 ? std::vector<uint8_t>{}
                            : (mask & 8 ? std::vector<uint8_t>{14, 14, 14} : std::vector<uint8_t>{12, 14, 18});
          switch (operation) {
            case 0:
              a.applyInitialOrientation();
              break;
            case 1:
              a.applyOrientation(static_cast<uint8_t>(argument));
              break;
            case 2:
              a.toggleAutoPageTurn(static_cast<uint8_t>(argument));
              break;
            case 3:
              a.applyReaderTextSettings();
              break;
            case 4:
              a.applyTextSettingLive();
              break;
            case 5:
              a.showTextRowPopup(argument);
              break;
          }
          if (a.overlayPopup.callback) {
            event("retained-result", result);
            auto callback = std::move(a.overlayPopup.callback);
            a.overlayPopup = Popup{};
            event("dismiss-popup");
            callback(result);
          }
          assert(lockDepth == 0);
          event("cached", a.cachedSpineIndex, a.cachedChapterTotalPageCount);
          event("page", a.nextPageNumber, bool(a.section));
          event("orientation-state", SETTINGS.orientation, a.appliedOrientation);
          event("auto", a.automaticPageTurnActive, a.pageTurnDuration);
          event("last-turn", a.lastPageTurnTime);
          event("font", SETTINGS.fontPointSize, SETTINGS.lineSpacing);
          event("alignment", SETTINGS.paragraphAlignment);
        }
      }
    }
  }
}
