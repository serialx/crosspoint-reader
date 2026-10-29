#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>

static unsigned long tick;
unsigned long millis() { return ++tick; }
#define LOG_ERR(tag, fmt, ...) std::printf("ERR " tag " " fmt "\n", ##__VA_ARGS__)
#define LOG_DBG(tag, fmt, ...) std::printf("DBG " tag " " fmt "\n", ##__VA_ARGS__)
namespace HalDisplay {
enum Mode { HALF_REFRESH, FAST_REFRESH };
enum class GrayscaleMode { Absolute, Overlay };
enum class GrayscaleBase { Separate, Combined };
enum class Controller { UC8279, UC8253, SSD1677, Other };
}  // namespace HalDisplay
namespace BoardConfig {
enum class Board { XteinkX4, Other };
struct {
  Board board;
} ACTIVE;
static bool a4;
bool isEegoA4() { return a4; }
}  // namespace BoardConfig
struct {
  bool x3;
  bool deviceIsX3() const { return x3; }
} gpio;
struct {
  bool textAntiAliasing;
  int getReaderFontId() const { return 12; }
  int getRefreshFrequency() const { return 3; }
} SETTINGS;
struct {
  size_t free, block;
  size_t getFreeHeap() const { return free; }
  size_t getMaxAllocHeap() const { return block; }
} ESP;
static int allocIndex, failedAlloc;
template <class T>
std::unique_ptr<T> makeUniqueNoThrow(size_t size) {
  std::printf("alloc %zu %d\n", size, allocIndex);
  if (++allocIndex == failedAlloc) return nullptr;
  return std::make_unique<T>(size);
}
struct FontCache {
  struct Scope {
    void endScanAndPrewarm() { std::puts("prewarm-end"); }
  };
  Scope createPrewarmScope() {
    std::puts("prewarm-begin");
    return {};
  }
} fontCache;
struct Caps {
  HalDisplay::GrayscaleBase base = HalDisplay::GrayscaleBase::Separate;
  bool stripUploads = false, asyncBase = false, absolute = false;
  bool supported() const { return absolute; }
};
struct XpPageRowBounds {};
struct XpPagePlan {
  void* commands;
  uint32_t capacity, length, valid;
};
struct XpDisplaySource {
  uint32_t size;
  void* context;
  uint8_t* scratch;
  uint32_t bytes;
  int rows, width;
  void (*paint)();
};
struct GfxRenderer {
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  Caps caps;
  bool baseOk = true, storeOk = true, planOk = true;
  XpPagePlan* plan = nullptr;
  FontCache* getFontCacheManager() { return &fontCache; }
  Caps grayscaleCapabilities(HalDisplay::GrayscaleMode = HalDisplay::GrayscaleMode::Overlay) const { return caps; }
  void displayBuffer(HalDisplay::Mode mode = HalDisplay::HALF_REFRESH) const { std::printf("display %d\n", mode); }
  void displayBufferAsync(HalDisplay::Mode mode) const { std::printf("async %d\n", mode); }
  void displayGrayscaleBase(HalDisplay::Mode mode) const { std::printf("base %d\n", mode); }
  bool displayGrayscaleBase(HalDisplay::GrayscaleMode mode, HalDisplay::Mode refresh) const {
    std::printf("absolute-base %d %d %d\n", int(mode), refresh, baseOk);
    return baseOk;
  }
  void clearScreen(uint8_t value = 0xff) { std::printf("clear %u\n", value); }
  void preconditionGrayscale() { std::puts("precondition"); }
  int getDisplayWidthBytes() const { return 2; }
  int getDisplayWidth() const { return 16; }
  int getDisplayHeight() const { return 83; }
  int getScreenHeight() const { return 83; }
  void setRenderMode(RenderMode mode) { std::printf("mode %d\n", mode); }
  void beginStripTarget(uint8_t*, int y, int rows) { std::printf("strip-begin %d %d\n", y, rows); }
  void endStripTarget() { std::puts("strip-end"); }
  void waitRefreshComplete() { std::puts("wait"); }
  void writeGrayscalePlaneStrip(bool lsb, const uint8_t*, int y, int rows) {
    std::printf("write %d %d %d\n", lsb, y, rows);
  }
  void displayGrayBuffer() { std::puts("gray-display"); }
  void cleanupGrayscaleWithFrameBuffer() { std::puts("cleanup"); }
  bool storeBwBuffer() {
    std::printf("store %d\n", storeOk);
    return storeOk;
  }
  void restoreBwBuffer() { std::puts("restore"); }
  void copyGrayscaleLsbBuffers() { std::puts("copy-lsb"); }
  void copyGrayscaleMsbBuffers() { std::puts("copy-msb"); }
  void usePagePlan(XpPagePlan* p, XpPageRowBounds*, int count, bool record = false) {
    plan = p;
    std::printf("plan %d %d %d\n", p != nullptr, count, record);
  }
};
struct {
  HalDisplay::Controller controller;
  bool sourceOk = true;
  HalDisplay::Controller getController() const { return controller; }
  bool isInverted() const { return false; }
  bool setFrameSource(const XpDisplaySource* source) {
    std::printf("source %d\n", source != nullptr);
    return sourceOk;
  }
} display;
struct Page {
  bool images = false, decode = false;
  int elements = 1;
  bool hasImages() const { return images; }
  bool hasImagesNeedingDecode() const { return decode; }
  int elementCount() const { return elements; }
  void render(GfxRenderer& r, int, int, int) const {
    std::puts("page");
    if (r.plan) r.plan->valid = r.planOk;
  }
  void renderImages(GfxRenderer&, int, int, int) const { std::puts("images"); }
  void renderWithImagePlaceholders(GfxRenderer&, int, int, int) const { std::puts("placeholders"); }
};
struct ImageBlock {
  static void clearRenderFailures() { std::puts("image-reset"); }
  static void releaseRenderCache() { std::puts("image-release"); }
  static void setRenderReadWindow(uint8_t* p, size_t size) { std::printf("image-window %d %zu\n", p != nullptr, size); }
};
struct ReaderStripBuffers {
  static constexpr int MAX_ELEMENTS = 512, COMMAND_BYTES = 16384, SCRATCH_BYTES = 8000, ROWS = 40;
  std::unique_ptr<uint8_t[]> scratch, commands, imageWindow;
  std::unique_ptr<XpPageRowBounds[]> bounds;
  bool prepareOk = true;
  bool prepare(int width, bool images) {
    std::printf("prepare %d %d %d\n", width, images, prepareOk);
    if (!prepareOk) return false;
    scratch = std::make_unique<uint8_t[]>(SCRATCH_BYTES);
    commands = std::make_unique<uint8_t[]>(COMMAND_BYTES);
    bounds = std::make_unique<XpPageRowBounds[]>(MAX_ELEMENTS);
    if (images) imageWindow = std::make_unique<uint8_t[]>(4096);
    return true;
  }
  void setSourceLoanActive(bool active) { std::printf("loan %d\n", active); }
  void releaseIdle() { std::puts("strips-release"); }
};
struct ReaderStripPainter {
  GfxRenderer& renderer;
  Page& page;
  int font, left, top, limit;
  bool inverted;
  void* context;
  void (*highlights)(void*, const Page&, int, int, int);
  static void paint() {}
};
struct EpubReaderActivity {
  GfxRenderer renderer;
  ReaderStripBuffers stripBuffers;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;
  void renderContents(std::unique_ptr<Page>, int, int, int, int);
  void renderStatusBar() const { std::puts("status"); }
  void drawClippingHighlights(const Page&, int, int, int) const { std::puts("highlights"); }
};
#include "render_methods.inc"
int main() {
  for (int board = 0; board < 5; ++board)
    for (int image = 0; image < 4; ++image)
      for (int flags = 0; flags < 16; ++flags)
        for (int remaining : {0, 2})
          for (int failure = 0; failure < 8; ++failure) {
            tick = 0;
            allocIndex = 0;
            failedAlloc = failure <= 2 ? failure : 0;
            BoardConfig::a4 = board == 4;
            gpio.x3 = board == 0;
            BoardConfig::ACTIVE.board = board == 1 ? BoardConfig::Board::XteinkX4 : BoardConfig::Board::Other;
            display.controller = board == 0   ? HalDisplay::Controller::UC8253
                                 : board == 1 ? HalDisplay::Controller::SSD1677
                                 : board == 2 ? HalDisplay::Controller::UC8279
                                              : HalDisplay::Controller::Other;
            display.sourceOk = failure != 7;
            SETTINGS.textAntiAliasing = flags & 1;
            ESP.free = failure == 3 ? 0 : 100000;
            ESP.block = failure == 3 ? 0 : 100000;
            EpubReaderActivity owner;
            owner.renderer.caps = {
                flags & 2 ? HalDisplay::GrayscaleBase::Combined : HalDisplay::GrayscaleBase::Separate, (flags & 4) != 0,
                (flags & 8) != 0, board == 2};
            owner.renderer.baseOk = failure != 4;
            owner.renderer.storeOk = failure != 5;
            owner.renderer.planOk = failure != 6;
            owner.stripBuffers.prepareOk = failure != 3;
            owner.pagesUntilFullRefresh = remaining;
            owner.forcedRefreshPending = failure == 4;
            auto page = std::make_unique<Page>();
            page->images = image != 0;
            page->decode = image == 2;
            page->elements = image == 3 ? 513 : 1;
            std::printf("case %d %d %d %d %d\n", board, image, flags, remaining, failure);
            owner.renderContents(std::move(page), 1, 2, 3, 4);
            std::printf("end %d %d %d\n", owner.pagesUntilFullRefresh, owner.forcedRefreshPending, allocIndex);
          }
}
