#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
enum StrId {
  STR_ALIGN_LEFT,
  STR_ALIGN_RIGHT,
  STR_AUTO_TURN_ENABLED,
  STR_BOOK_S_STYLE,
  STR_BT_CONNECTED_TO,
  STR_BT_CONNECTING,
  STR_BT_FAILED,
  STR_BT_LOW_MEMORY,
  STR_CENTER,
  STR_EXTRA_WIDE,
  STR_FOCUS_READING,
  STR_FONT,
  STR_FONT_SIZE,
  STR_JUSTIFY,
  STR_LANDSCAPE_CCW,
  STR_LANDSCAPE_CW,
  STR_LINE_SPACING,
  STR_NORMAL,
  STR_NOTO_SANS,
  STR_ORIENTATION_INVERTED,
  STR_PARA_ALIGNMENT,
  STR_PORTRAIT,
  STR_STATE_OFF,
  STR_TIGHT,
  STR_UNNAMED,
  STR_WIDE
};
const char* tr(StrId id) {
  static const char* names[] = {
      "STR_ALIGN_LEFT",     "STR_ALIGN_RIGHT",   "STR_AUTO_TURN_ENABLED", "STR_BOOK_S_STYLE",
      "Connected %s",       "STR_BT_CONNECTING", "STR_BT_FAILED",         "STR_BT_LOW_MEMORY",
      "STR_CENTER",         "STR_EXTRA_WIDE",    "STR_FOCUS_READING",     "STR_FONT",
      "STR_FONT_SIZE",      "STR_JUSTIFY",       "STR_LANDSCAPE_CCW",     "STR_LANDSCAPE_CW",
      "STR_LINE_SPACING",   "STR_NORMAL",        "STR_NOTO_SANS",         "STR_ORIENTATION_INVERTED",
      "STR_PARA_ALIGNMENT", "STR_PORTRAIT",      "STR_STATE_OFF",         "STR_TIGHT",
      "STR_UNNAMED",        "STR_WIDE"};
  return names[id];
}
struct {
  const char* get(StrId id) const { return tr(id); }
} I18N;
struct CrossPointSettings {
  enum STATUS_BAR_TITLE { BOOK_TITLE, CHAPTER_TITLE, HIDE_TITLE };
  enum {
    LINE_COMPRESSION_COUNT = 4,
    PARAGRAPH_ALIGNMENT_COUNT = 5,
    FONT_FAMILY_COUNT = 1,
    ORIENTATION_COUNT = 4,
    READER_MENU_TOOLBAR = 1
  };
  struct Spec {
    uint8_t titleMode = BOOK_TITLE;
    bool showsTitle() const { return titleMode != HIDE_TITLE; }
  } spec;
  uint8_t fontFamily = 0, fontPointSize = 12, lineSpacing = 0, paragraphAlignment = 0, orientation = 0;
  uint8_t readerMenuStyle = 0;
  char sdFontFamilyName[32] = {};
  Spec statusBarSpec() const { return spec; }
} SETTINGS;
constexpr StrId kTextRowNames[] = {StrId::STR_FONT, StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT, StrId::STR_FOCUS_READING};
constexpr StrId kSpacingIds[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowNames));
static_assert(std::size(kSpacingIds) == CrossPointSettings::LINE_COMPRESSION_COUNT, "line spacing labels");
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");

constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
namespace bleturner {
enum class LinkNote : uint8_t { None, Connecting, Failed, Connected, LowMemory };
LinkNote note;
struct Peer {
  char name[32] = "Keyboard";
  char addr[32] = "AA:BB";
} peer;
LinkNote linkNote() { return note; }
Peer linked() { return peer; }
}  // namespace bleturner
struct LinkNoteTitle {
  bleturner::LinkNote draw(bool title, bleturner::LinkNote value) const {
    std::printf("note %d %d\n", title, static_cast<int>(value));
    return title ? value : bleturner::LinkNote::None;
  }
};
struct UITheme {
  uint8_t status = 20;
  struct Metrics {
    uint8_t statusBarVerticalMargin = 5;
  } metrics;
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  uint8_t getStatusBarHeight() const { return status; }
  uint8_t getProgressBarHeight() const { return 4; }
  const Metrics& getMetrics() const { return metrics; }
};
struct Epub {
  int toc = 0;
  float progress = .3f;
  int getTocIndexForSpineIndex(int spine) const {
    std::printf("toc %d\n", spine);
    return toc;
  }
  struct Item {
    std::string title;
  };
  Item getTocItem(int index) const {
    std::printf("item %d\n", index);
    return {"Chapter Ω"};
  }
  std::string getTitle() const { return "Book 한글"; }
  float calculateProgress(int spine, float fraction) const {
    std::printf("progress %d %a\n", spine, double(fraction));
    return progress;
  }
};
struct Section {
  int currentPage = 0;
  uint16_t count = 10;
  bool building = false;
  uint16_t estimatedTotalPages() const { return count; }
  bool isBuilding() const { return building; }
};
struct GfxRenderer {};
struct {
  void drawStatusBar(GfxRenderer&, float progress, int page, float count, const std::string& title, int x, int y,
                     bool chapter, bool bookmark, bool building) const {
    std::printf("status %a %d %a [%s] %d %d %d %d %d\n", double(progress), page, double(count), title.c_str(), x, y,
                chapter, bookmark, building);
  }
} GUI;
struct EpubReaderMenuActivity {
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
};
struct EpubReaderActivity {
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  mutable GfxRenderer renderer;
  LinkNoteTitle linkNoteTitle;
  int currentSpineIndex = 2, autoTurnOption = 0;
  bool automaticPageTurnActive = false, currentPageBookmarked = false;
  unsigned long pageTurnDuration = 10000;
  std::vector<EpubReaderMenuActivity::MenuItem> moreItems;
  void renderStatusBar() const;
  std::string readerViewValue(uint8_t operation, int32_t row = 0) const;
  bool usesToolbarMenu() const;
  std::string currentChapterTitle() const;
  std::string textRowName(int row) const;
  std::string textRowValue(int row) const;
  std::string moreRowName(int row) const;
  std::string moreRowValue(int row) const;
};
// VALUE_METHODS
int main() {
  for (int note = 0; note < 5; ++note)
    for (int title = 0; title < 3; ++title)
      for (int flags = 0; flags < 64; ++flags) {
        EpubReaderActivity a;
        bleturner::note = static_cast<bleturner::LinkNote>(note);
        SETTINGS.spec.titleMode = title;
        a.automaticPageTurnActive = flags & 1;
        a.currentPageBookmarked = flags & 2;
        if (flags & 4)
          a.epub.reset();
        else {
          a.epub->toc = flags & 8 ? -1 : 2;
          a.epub->progress = flags & 16 ? 1.25f : .3f;
        }
        if (flags & 8)
          a.section.reset();
        else {
          a.section->count = flags & 16 ? 0 : 10;
          a.section->currentPage = flags & 32 ? 15 : 0;
          a.section->building = flags & 2;
        }
        UITheme::getInstance().status = flags & 16 ? 0 : (flags & 32 ? 4 : 20);
        bleturner::peer.name[0] = flags & 32 ? '\0' : 'K';
        a.pageTurnDuration = flags & 2 ? 5000 : 10000;
        std::printf("case %d %d %d\n", note, title, flags);
        a.renderStatusBar();
        std::printf("chapter [%s]\n", a.currentChapterTitle().c_str());
      }
  for (int value : {0, 1, 3, 4, 5, 12, 255})
    for (int sd = 0; sd < 2; ++sd) {
      EpubReaderActivity a;
      SETTINGS.fontFamily = SETTINGS.fontPointSize = SETTINGS.lineSpacing = SETTINGS.paragraphAlignment =
          SETTINGS.orientation = value;
      SETTINGS.readerMenuStyle = value;
      std::snprintf(SETTINGS.sdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName), "%s", sd ? "Family 日本語" : "");
      std::printf("values %d %d toolbar=%d\n", value, sd, a.usesToolbarMenu());
      for (int row = -1; row <= 6; ++row)
        std::printf("text %d [%s] [%s]\n", row, a.textRowName(row).c_str(), a.textRowValue(row).c_str());
      for (int action = 0; action < 19; ++action) {
        a.moreItems = {{static_cast<EpubReaderMenuActivity::MenuAction>(action), STR_UNNAMED}};
        for (int rate = 0; rate <= 6; ++rate) {
          a.autoTurnOption = rate;
          for (int row = -1; row <= 1; ++row)
            std::printf("more %d %d %d [%s] [%s]\n", action, rate, row, a.moreRowName(row).c_str(),
                        a.moreRowValue(row).c_str());
        }
      }
    }
}
