#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
namespace freeink::ui {
struct Rect {
  int16_t x = 0, y = 0, width = 0, height = 0;
};
}  // namespace freeink::ui
namespace fui = freeink::ui;
struct Rect {
  int x, y, width, height;
};
enum class Color { LightGray };
static bool allocationFailure, copyFailure, restoreFailure;
static int bitmapStatus, paintTrace;
#define LOG_ERR(...)
namespace HalMemory {
using PsramBuffer = std::unique_ptr<uint8_t[]>;
PsramBuffer allocatePsram(size_t size) { return allocationFailure ? nullptr : std::make_unique<uint8_t[]>(size); }
}  // namespace HalMemory
class GfxRenderer {
 public:
  int orientation = 0;
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  int getOrientation() const { return orientation; }
  size_t getRegionByteSize(int x, int, int w, int h) const { return size_t((x + w + 7) / 8 - x / 8) * h; }
  bool copyBufferToRegion(int, int, int, int, const uint8_t*, size_t) {
    paintTrace |= 32;
    return !restoreFailure;
  }
  bool copyRegionToBuffer(int, int, int, int, uint8_t*, size_t) {
    paintTrace |= 64;
    return !copyFailure;
  }
  void drawRect(int, int, int, int) { paintTrace |= 2; }
  void fillRect(int, int, int, int) { paintTrace |= 4; }
  void fillRectDither(int, int, int, int, Color) { paintTrace |= 8; }
};
struct HalFile {
  void close() {}
};
enum class BmpReaderError { Ok, Invalid };
struct Bitmap {
  explicit Bitmap(HalFile&) {}
  BmpReaderError parseHeaders() const { return bitmapStatus == 1 ? BmpReaderError::Invalid : BmpReaderError::Ok; }
  int getWidth() const { return 91; }
  int getHeight() const { return 140; }
};
struct StorageType {
  bool openFileForRead(const char*, const std::string&, HalFile&) { return bitmapStatus != 2; }
} Storage;
struct Theme {
  bool drawCoverThumbFill(GfxRenderer&, const Bitmap&, Rect, int) {
    paintTrace |= 1;
    return bitmapStatus != 3;
  }
  void drawCoverPlaceholder(GfxRenderer&, Rect) { paintTrace |= 16; }
} GUI;
class HomeCoverCache {
 public:
  static constexpr size_t MAX_COVERS = 7;
  struct CachedCover {
    fui::Rect rect{};
    size_t offset = 0, bytes = 0;
    bool valid = false;
  };
  HalMemory::PsramBuffer coverCache;
  std::array<CachedCover, MAX_COVERS> cachedCovers{};
  size_t coverCacheCapacity = 0, coverCacheUsed = 0;
  int coverCacheOrientation = -1;
  HalFile coverFile;
  Bitmap coverBitmap{coverFile};
  GfxRenderer& renderer;
  explicit HomeCoverCache(GfxRenderer& r) : renderer(r) {}
  void begin();
  void prepare();
  void invalidate();
  void invalidate(size_t);
  bool paint(fui::Rect, size_t, const std::string&);
};
