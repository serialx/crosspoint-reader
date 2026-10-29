#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Platform/resource leaves record effects; policy bodies are frozen verbatim.
struct RenderLock {};
struct Rect {
  int x, y, w, h;
};
struct EpdFontFamily {
  enum Style { REGULAR, BOLD };
};
constexpr int UI_10_FONT_ID = 10, SMALL_FONT_ID = 5;
constexpr int Logo120 = 1, MoonIcon = 2, MOONICON_HEIGHT = 48, MOONICON_WIDTH = 48;
constexpr auto CROSSPOINT_VERSION = "version";
enum {
  STR_CROSSPOINT,
  STR_BOOTING,
  STR_SLEEPING,
  STR_ENTERING_SLEEP,
  STR_CRASH_TITLE,
  STR_CRASH_DESCRIPTION,
  STR_CRASH_REASON,
  STR_BACK,
  STR_SET_SLEEP_COVER_CONFIRM
};
const char* tr(int id) {
  static const char* s[] = {"CrossPoint",  "Booting", "Sleeping", "Entering sleep", "Crash",
                            "Description", "Reason",  "Back",     "Set cover?"};
  return s[id];
}
struct GfxRenderer {
  enum Orientation { Portrait };
  int w = 528, h = 792;
  int getScreenWidth() const { return w; }
  int getScreenHeight() const { return h; }
  void clearScreen() { puts("clear"); }
  void drawImage(int id, int x, int y, int w, int h) { printf("image %d %d %d %d %d\n", id, x, y, w, h); }
  void drawCenteredText(int f, int y, const char* s, bool = true, int style = 0) {
    printf("center %d %d %d %s\n", f, y, style, s);
  }
  void drawText(int f, int x, int y, const char* s) { printf("text %d %d %d %s\n", f, x, y, s); }
  void displayBuffer(int mode = 0) { printf("refresh %d\n", mode); }
  void displayGrayscaleBase(int mode) { printf("gray-base %d\n", mode); }
  void invertScreen() { puts("invert-frame"); }
  void setOrientation(Orientation) { puts("portrait"); }
  int getLineHeight(int) const { return 23; }
  std::vector<std::string> wrappedText(int, const char* text, int width, int lines) {
    printf("wrap %d %d %s\n", width, lines, text);
    return {"first", "second"};
  }
};
namespace HalDisplay {
constexpr int FAST_REFRESH = 0;
}
constexpr int kSleepClean = 1;
struct Display {
  bool inverted = false;
  bool isInverted() { return inverted; }
  void setInverted(bool v) { printf("polarity %d\n", v); }
} display;
struct GPIO {
  bool x3 = false;
  bool deviceIsX3() { return x3; }
} gpio;
struct CrossPointSettings {
  enum SLEEP_SCREEN_MODE {
    DARK,
    LIGHT,
    CUSTOM,
    COVER,
    COVER_CUSTOM,
    BLANK,
    QUICK_RESUME,
    TRANSPARENT_CUSTOM,
    COVER_TRANSPARENT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT, CROP };
  int sleepScreenCoverMode = 0;
  enum QUICK_RESUME_SLEEP_SCREEN { DISABLED, QUICK_RESUME_AFTER_TIMEOUT };
  int sleepScreen = 0, quickResumeSleepScreen = 0, orientation = 0;
} SETTINGS;
struct {
  bool lastSleepFromReader = false;
} APP_STATE;
namespace ReaderUtils {
void applyOrientation(GfxRenderer&, int value) { printf("orientation %d\n", value); }
}  // namespace ReaderUtils
struct Metrics {
  int contentSidePadding = 10, topPadding = 12, headerHeight = 40, verticalSpacing = 8;
};
struct UITheme {
  Metrics m;
  static UITheme& getInstance() {
    static UITheme v;
    return v;
  }
  const Metrics& getMetrics() { return m; }
};
struct Gui {
  void drawPopup(GfxRenderer&, const char* s) { printf("popup %s\n", s); }
  void drawHeader(GfxRenderer&, Rect r, const char* s, void*, bool) {
    printf("header %d %d %d %d %s\n", r.x, r.y, r.w, r.h, s);
  }
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) { puts("hints"); }
} GUI;
bool drawSleepPopupPreservingFrame(GfxRenderer&) {
  puts("preserved-popup");
  return true;
}
void releaseSdFontCachesForDecode(GfxRenderer&) { puts("release-fonts"); }
struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Up, Right, Down };
  enum class SwipeDir { None, Left, Right };
  unsigned pressed = 0, released = 0;
  bool tap = false;
  SwipeDir swipe = SwipeDir::None;
  bool wasPressed(Button b) { return pressed & (1u << static_cast<int>(b)); }
  bool wasReleased(Button b) { return released & (1u << static_cast<int>(b)); }
  bool wasScreenTapped(int&, int&) { return tap; }
  SwipeDir wasSwipe() { return swipe; }
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) { return {a, b, c, d}; }
};
struct Activity {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  void onEnter() {}
  void loop() {}
  void finish() { puts("finish"); }
};
struct BootActivity : Activity {
  void onEnter();
};
struct SleepActivity : Activity {
  bool fromTimeout = false;
  void onEnter();
  void renderDefaultSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderLastScreenSleepScreen() const;
  void renderCoverSleepScreen() const { puts("cover"); }
  void renderCustomSleepScreen() const { puts("custom"); }
  void renderTransparentCustomSleepScreen() const { puts("overlay"); }
  mutable GfxRenderer renderer;
};
struct CrashActivity : Activity {
  std::string panicMessage = "panic";
  void loop();
  void render(RenderLock&&);
};
struct Xtc {
  unsigned count = 3;
  unsigned getPageCount() const { return count; }
};
struct XtcReaderActivity {
  Xtc* xtc;
  unsigned currentPage = 0;
  bool pageTurn(bool);
  bool skipPages(int);
  bool isAtEndOfBook() const;
  void onReturnFromEndOfBook();
};
namespace FsHelpers {
std::string extractFolderPath(const std::string&) { return "/pics"; }
}  // namespace FsHelpers
struct ActivityResult {
  bool isCancelled = false;
};
struct ConfirmationActivity {
  ConfirmationActivity(GfxRenderer&, MappedInputManager&, const char*, std::string) {}
};
template <class T, class... A>
std::unique_ptr<T> makeUniqueNoThrow(A&&... a) {
  return std::make_unique<T>(std::forward<A>(a)...);
}
#define LOG_ERR(...) ((void)0)
struct Manager {
  void goToFileBrowser(const std::string&) { puts("browser"); }
};
struct BmpViewerActivity : Activity {
  std::string filePath = "/pics/b.bmp";
  std::vector<std::string> siblingImages{"a.bmp", "b.bmp", "c.bmp"};
  int currentImageIndex = 1;
  bool eligible = true;
  Manager activityManager;
  void loop();
  bool canSetSleepCover() const { return eligible; }
  void onEnter() { printf("open %d\n", currentImageIndex); }
  void doSetSleepCover() { puts("save-cover"); }
  void startActivityForResult(std::unique_ptr<ConfirmationActivity>, auto callback) {
    puts("confirmation");
    callback(ActivityResult{});
  }
};
struct BitmapPlacement {
  int x = 0, y = 0;
  float cropX = 0, cropY = 0;
};
#include "baseline.inc"
static void pagePixels(unsigned width, unsigned height, unsigned depth, unsigned pass) {
  const unsigned planeSize = width * ((height + 7) / 8);
  const unsigned rowBytes = (width + 7) / 8;
  std::vector<unsigned char> data(depth == 2 ? planeSize * 2 : rowBytes * height);
  for (unsigned i = 0; i < data.size(); ++i) data[i] = static_cast<unsigned char>(i * 73 + 41);
  std::vector<unsigned char> pixels(width * height, pass == 0 ? 0 : 1);
  const auto getPixelValue = [&](unsigned x, unsigned y) -> unsigned char {
    const size_t colIndex = width - 1 - x;
    const size_t byteInCol = y / 8;
    const size_t bitInByte = 7 - (y % 8);
    const size_t byteOffset = colIndex * ((height + 7) / 8) + byteInCol;
    const unsigned char bit1 = (data[byteOffset] >> bitInByte) & 1;
    const unsigned char bit2 = (data[planeSize + byteOffset] >> bitInByte) & 1;
    return (bit1 << 1) | bit2;
  };
  for (unsigned y = 0; y < height; ++y)
    for (unsigned x = 0; x < width; ++x) {
      if (depth == 2) {
        const auto value = getPixelValue(x, y);
        if (pass == 0 && value >= 1) pixels[y * width + x] = 1;
        if (pass == 1 && value == 1) pixels[y * width + x] = 0;
        if (pass == 2 && (value == 1 || value == 2)) pixels[y * width + x] = 0;
      } else if (!((data[y * rowBytes + x / 8] >> (7 - x % 8)) & 1))
        pixels[y * width + x] = 1;
    }
  printf("pixels %u %u %u %u ", width, height, depth, pass);
  for (auto pixel : pixels) putchar('0' + pixel);
  putchar('\n');
}
int main() {
  for (auto size : {std::pair{1u, 1u}, std::pair{8u, 8u}, std::pair{17u, 13u}, std::pair{31u, 19u}})
    for (unsigned depth : {1u, 2u})
      for (unsigned pass = 0; pass < (depth == 2 ? 3u : 1u); ++pass) pagePixels(size.first, size.second, depth, pass);

  for (const auto screen : {std::pair{528, 792}, std::pair{480, 800}, std::pair{792, 528}, std::pair{800, 480}})
    for (const auto image :
         {std::pair{1, 1}, std::pair{120, 120}, std::pair{800, 600}, std::pair{500, 1000}, std::pair{2048, 3072}})
      for (int crop = 0; crop < 2; ++crop) {
        GfxRenderer r;
        r.w = screen.first;
        r.h = screen.second;
        SETTINGS.sleepScreenCoverMode = crop;
        const auto p = calculateBitmapPlacement(image.first, image.second, r);
        unsigned cx, cy;
        std::memcpy(&cx, &p.cropX, 4);
        std::memcpy(&cy, &p.cropY, 4);
        printf("placement %d %d %d %d %d %d %d %u %u\n", image.first, image.second, r.w, r.h, crop, p.x, p.y, cx, cy);
      }

  for (const auto size : {std::pair{528, 792}, std::pair{480, 800}, std::pair{792, 528}, std::pair{800, 480}}) {
    printf("screen %d %d\n", size.first, size.second);
    BootActivity boot;
    boot.renderer.w = size.first;
    boot.renderer.h = size.second;
    boot.onEnter();
    SleepActivity sleep;
    sleep.renderer.w = size.first;
    sleep.renderer.h = size.second;
    for (int mode = 0; mode < 10; ++mode) {
      SETTINGS.sleepScreen = mode;
      printf("default %d\n", mode);
      sleep.renderDefaultSleepScreen();
    }
    for (int x3 = 0; x3 < 2; ++x3) {
      gpio.x3 = x3;
      sleep.renderLastScreenSleepScreen();
    }
    sleep.renderBlankSleepScreen();
    CrashActivity crash;
    crash.renderer.w = size.first;
    crash.renderer.h = size.second;
    crash.render({});
  }
  for (int mode = 0; mode < 10; ++mode)
    for (int timeout = 0; timeout < 2; ++timeout)
      for (int quick = 0; quick < 2; ++quick)
        for (int reader = 0; reader < 2; ++reader)
          for (int inverted = 0; inverted < 2; ++inverted) {
            printf("sleep %d %d %d %d %d\n", mode, timeout, quick, reader, inverted);
            SETTINGS.sleepScreen = mode;
            SETTINGS.quickResumeSleepScreen = quick;
            APP_STATE.lastSleepFromReader = reader;
            display.inverted = inverted;
            SleepActivity sleep;
            sleep.fromTimeout = timeout;
            sleep.onEnter();
          }
  for (int press = 0; press < 2; ++press)
    for (int tap = 0; tap < 2; ++tap) {
      printf("crash %d %d\n", press, tap);
      CrashActivity c;
      c.mappedInput.pressed = press;
      c.mappedInput.tap = tap;
      c.loop();
    }
  for (unsigned count : {0u, 1u, 3u})
    for (unsigned page = 0; page <= count + 1; ++page)
      for (int amount : {-10, -1, 0, 1, 10}) {
        Xtc book{count};
        XtcReaderActivity r{&book, page};
        bool changed = r.skipPages(amount);
        printf("skip %u %u %d %d %u %d\n", count, page, amount, changed, r.currentPage, r.isAtEndOfBook());
      }
  for (unsigned count : {0u, 1u, 3u})
    for (unsigned page = 0; page <= count + 1; ++page)
      for (bool next : {false, true}) {
        Xtc book{count};
        XtcReaderActivity r{&book, page};
        bool changed = r.pageTurn(next);
        printf("turn %u %u %d %d %u\n", count, page, next, changed, r.currentPage);
        r.onReturnFromEndOfBook();
        printf("return %u\n", r.currentPage);
      }
  for (int index = -1; index < 4; ++index)
    for (unsigned released = 0; released < 64; ++released)
      for (int swipe = 0; swipe < 3; ++swipe) {
        printf("bmp %d %u %d\n", index, released, swipe);
        BmpViewerActivity b;
        b.currentImageIndex = index;
        b.mappedInput.released = released;
        b.mappedInput.swipe = static_cast<MappedInputManager::SwipeDir>(swipe);
        b.loop();
      }
}
