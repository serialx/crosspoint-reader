#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

struct Rect {
  int x, y, width, height;
};
enum class Color { Black, White, LightGray };
namespace EpdFontFamily {
enum Style { REGULAR, BOLD };
}
constexpr int UI_12_FONT_ID = 12, UI_10_FONT_ID = 10, SMALL_FONT_ID = 8;
constexpr int STR_NO_OPEN_BOOK = 0, STR_START_READING = 1, STR_CONTINUE_READING = 2;
const char* tr(int id) { return id == 0 ? "No open book" : id == 1 ? "Start reading" : "Continue reading"; }
#define LOG_DBG(...)
struct RecentBook {
  std::string title, author, coverBmpPath;
};
struct Metrics {
  int contentSidePadding = 20, homeCoverHeight = 180, verticalSpacing = 10, menuRowHeight = 42, menuSpacing = 6,
      homeRecentBooksCount = 3, scrollBarWidth = 4, scrollBarRightOffset = 6;
};
struct BaseMetrics {
  static inline Metrics values;
};
struct LyraMetrics {
  static inline Metrics values;
};
struct Lyra3CoversMetrics {
  static inline Metrics values;
};
struct RoundedRaffMetrics {
  static inline Metrics values;
};
struct UITheme {
  static std::string getCoverThumbPath(const std::string& path, int) { return path; }
};
static int screenWidth, coverStatus, bitmapWidth;
static uint8_t CoverIcon[1];
enum UIIcon { FirstIcon, SecondIcon, ThirdIcon, FourthIcon, FifthIcon, SixthIcon };
static uint8_t menuIcons[6];
const uint8_t* iconForName(UIIcon icon) { return &menuIcons[icon]; }
struct HalFile {
  int id = 0;
  void close() {}
};
struct StorageType {
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    file.id = path.empty() ? 0 : path[0] - '0';
    return coverStatus != 1;
  }
} Storage;
enum class BmpReaderError { Ok, Invalid };
struct Bitmap {
  int id;
  explicit Bitmap(HalFile& file) : id(file.id) {}
  BmpReaderError parseHeaders() const { return coverStatus == 2 ? BmpReaderError::Ok : BmpReaderError::Invalid; }
  int getWidth() const { return bitmapWidth; }
  int getHeight() const { return 270; }
};
void primitive(int kind, int x, int y, int w, int h, int paint = 0, int radius = 0, int corners = 0,
               uint32_t resource = 0) {
  std::printf("p %d %d %d %d %d %d %d %d %u\n", kind, x, y, w, h, paint, radius, corners, resource);
}
struct GfxRenderer {
  int getScreenWidth() const { return screenWidth; }
  int getLineHeight(int font) const { return font * 2; }
  int getTextWidth(int font, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return std::strlen(text) * (font / 2 + int(style));
  }
  std::string truncatedText(int font, const char* text, int width,
                            EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    return std::string(text).substr(0, std::max(0, width) / (font / 2 + int(style)));
  }
  std::vector<std::string> wrappedText(int font, const char* text, int width, int lines,
                                       EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    std::vector<std::string> result;
    std::string rest = text;
    const auto chars = std::max(1, width / (font / 2 + int(style)));
    for (int i = 0; i < lines && !rest.empty(); ++i) {
      result.push_back(rest.substr(0, chars));
      rest.erase(0, chars);
    }
    return result;
  }
  void drawText(int font, int x, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    std::printf("t %d %d %d %d %d %s\n", font, x, y, !black, int(style), text);
  }
  void drawCenteredText(int font, int y, const char* text, bool black = true) const {
    drawText(font, (screenWidth - getTextWidth(font, text)) / 2, y, text, black);
  }
  void drawRect(int x, int y, int w, int h, bool black = true) const { primitive(1, x, y, w, h, !black); }
  void fillRect(int x, int y, int w, int h, bool black = true) const { primitive(0, x, y, w, h, !black); }
  void fillRectDither(int x, int y, int w, int h, Color color) const { primitive(0, x, y, w, h, int(color)); }
  void fillRoundedRect(int x, int y, int w, int h, int radius, Color color) const {
    primitive(0, x, y, w, h, int(color), radius);
  }
  void fillRoundedRect(int x, int y, int w, int h, int radius, bool tl, bool tr, bool bl, bool br, Color color) const {
    primitive(0, x, y, w, h, int(color), radius, tl + 2 * tr + 4 * bl + 8 * br);
  }
  void drawRoundedRect(int x, int y, int w, int h, int, int radius, bool black) const {
    primitive(1, x, y, w, h, !black, radius);
  }
  void maskRoundedRectOutsideCorners(int x, int y, int w, int h, int, Color color) const {
    primitive(5, x, y, w, h, int(color), 0, 0, 0x80000005u);
  }
  void drawIcon(const uint8_t* icon, int x, int y, int size) const {
    primitive(5, x, y, size, size, 0, 0, 0, icon == CoverIcon ? 0x80000008u : 0x5000u + unsigned(icon - menuIcons));
  }
  void fillPolygon(const int* x, const int* y, int, bool black) const {
    primitive(5, x[0], y[0], x[1] - x[0], y[2] - y[0], !black, 0, 0, 0x80000006u);
  }
};
void drawCoverThumbFill(GfxRenderer&, Bitmap& image, Rect rect) {
  primitive(5, rect.x, rect.y, rect.width, rect.height, 0, 0, 0, 0x1000u + image.id);
}
void drawCoverPlaceholder(GfxRenderer&, Rect rect) {
  primitive(5, rect.x, rect.y, rect.width, rect.height, 0, 0, 0, 0x80000007u);
}
#define COVER_METHOD                                                                                     \
  void drawRecentBookCover(GfxRenderer&, Rect, const std::vector<RecentBook>&, int, bool&, bool&, bool&, \
                           std::function<bool()>) const
#define MENU_METHOD                                                                         \
  void drawButtonMenu(GfxRenderer&, Rect, int, int, const std::function<std::string(int)>&, \
                      const std::function<UIIcon(int)>&) const
class BaseTheme {
 public:
  COVER_METHOD;
  MENU_METHOD;
};
class LyraTheme {
 public:
  COVER_METHOD;
  MENU_METHOD;
  void drawEmptyRecents(const GfxRenderer&, Rect) const;
};
class Lyra3CoversTheme : public LyraTheme {
 public:
  COVER_METHOD;
  int homeCoverThumbHeight(const GfxRenderer&) const;
};
class RoundedRaffTheme {
 public:
  COVER_METHOD;
  MENU_METHOD;
  int getMenuRowHeight(const GfxRenderer&) const;
};
constexpr int hPaddingInSelection = 8, cornerRadius = 6, mainMenuIconSize = 32;
constexpr int kCoverRadius = 18, kMenuRadius = 30, kRowRadius = 20, kInteractiveInsetX = 20, kSelectableRowGap = 6,
              kTitleFontId = 12;
static int coverWidth = 0;
