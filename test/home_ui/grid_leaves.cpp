#include <array>
#include <string>
#include <utility>
#include <vector>
using UiScreen = ui::Screen<24>;
namespace fui = freeink::ui;
struct Rect {
  int x, y, width, height;
};
struct RecentBook {
  std::string title, author, coverBmpPath;
};
static const uint8_t FolderIcon[1]{}, LibraryIcon[1]{}, BlocksIcon[1]{}, TransferIcon[1]{}, Settings2Icon[1]{},
    BookIcon[1]{};
constexpr int STR_NO_OPEN_BOOK = 0, STR_START_READING = 1;
const char* tr(int id) { return id == 0 ? "No open book" : "Start reading"; }
struct GfxRenderer {
  int width = 528, height = 792;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  void drawIcon(const uint8_t* icon, int x, int y, int size) {
    const int id = icon == FolderIcon      ? 0
                   : icon == LibraryIcon   ? 1
                   : icon == BlocksIcon    ? 2
                   : icon == TransferIcon  ? 3
                   : icon == Settings2Icon ? 4
                                           : 5;
    std::printf("icon %d %d %d %d\n", id, x, y, size);
  }
};
struct ThemeMetrics {
  int topPadding = 10, batteryBarHeight = 36, headerSidePadding = 20, coverGridTabBarHeight = 50;
};
struct BoardConfig {
  struct Board {
    int cornerRadius = 0, tabBarHeightPct = 100, tabBarOffsetY = 0, tabBarWidthPct = 100;
  };
  static Board ACTIVE;
  static bool touch;
  static bool hasTouch() { return touch; }
};
BoardConfig::Board BoardConfig::ACTIVE;
bool BoardConfig::touch = false;
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  Rect getScreenSafeArea(GfxRenderer& r, bool) { return {4, 8, r.width - 8, r.height - 44}; }
  const ThemeMetrics& getMetrics() const {
    static ThemeMetrics m;
    return m;
  }
  void drawHeader(GfxRenderer&, Rect r, const char*) {
    std::printf("header %d %d %d %d\n", r.x, r.y, r.width, r.height);
  }
};
#define GUI UITheme::getInstance()
struct Cache {
  void prepare() {}
  bool paint(fui::Rect r, size_t index, const std::string&) {
    Recorder::rect("cover", r);
    std::printf(" %zu\n", index);
    return true;
  }
};
constexpr fui::ActionId SELECT = 1;
constexpr int16_t COVER_CELL_INSET = 6;
constexpr int COVER_ROW_EXTRA_HEIGHT = 8;
class CoverGridHomeUi {
 public:
  static constexpr int GRID_COLUMNS = 3, GRID_ROWS = 2, MAX_BOOKS = 7;
  explicit CoverGridHomeUi(GfxRenderer& r) : renderer(r) {}
  Cache coverCache;
  GfxRenderer& renderer;
  const std::vector<RecentBook>* books;
  std::array<std::string, 7> coverPaths;
  int selected = 0, progress = -1, thumbHeight = 0;
  bool hasOpds = false;
  char progressText[12]{};
  fui::BookCardProps card;
  fui::CoverGridProps grid;
  fui::Rect gridBounds{};
  fui::TabBarProps tabs;
  std::array<fui::TabItem, 5> tabItems;
  void noteThumbHeight(int height) { thumbHeight = std::max(1, height + 8); }
  void draw(UiScreen&);
  void drawHeaderBand();
  void drawEmpty(UiScreen&);
  void drawCurrent(UiScreen&, fui::Rect, int);
  fui::Rect layoutGrid(fui::Rect);
  void drawGrid(UiScreen&);
  void drawTabs(UiScreen&, fui::Rect);
  bool paintFramedCover(fui::DrawTarget&, fui::Rect, size_t);
};
