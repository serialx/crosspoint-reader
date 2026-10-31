#include <xp_ui_reader.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
static unsigned locks;
static void event(const char* text, int a = 0, int b = 0) { std::printf("%s %d %d lock=%u\n", text, a, b, locks); }
#define LOG_DBG(tag, format, ...) std::printf(tag " " format "\n", __VA_ARGS__)
struct RenderLock {
  RenderLock() {
    ++locks;
    event("lock");
  }
  ~RenderLock() {
    event("unlock");
    assert(locks);
    --locks;
  }
};
struct Section {
  int currentPage = 3;
  uint16_t pageCount = 20;
  bool building = false;
  std::optional<uint16_t> offsetPage;
  std::optional<uint32_t> offset;
  ~Section() { event("drop-section"); }
  bool isBuilding() const {
    event("building", building);
    return building;
  }
  uint16_t estimatedTotalPages() const {
    event("page-count", pageCount);
    return pageCount;
  }
  std::optional<uint16_t> getPageForVisibleTextOffset(uint32_t value) const {
    event("map-offset", int(value));
    return offsetPage;
  }
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t page) const {
    event("get-offset", page);
    return offset;
  }
};
struct Epub {
  uint32_t size = 1000, cumulative = 1000, previous = 100;
  int spineCount = 7, found = 2, resolved = 4;
  uint32_t getBookSize() const {
    event("book-size", int(size));
    return size;
  }
  int getSpineItemsCount() const {
    event("spine-count", spineCount);
    return spineCount;
  }
  int getSpineIndexForSize(uint32_t target) const {
    event("find-spine", int(target), found);
    return found;
  }
  uint32_t getCumulativeSpineItemSize(int spine) const {
    event("cumulative", spine);
    return spine == found - 1 ? previous : cumulative;
  }
  int resolveHrefToSpineIndex(const std::string& href) const {
    std::printf("resolve [%s] %d\n", href.c_str(), resolved);
    return resolved;
  }
};
struct CrossPointPosition {
  int spineIndex, pageNumber;
};
struct SavedProgressPosition {
  std::string xpath;
  float percentage;
};
struct ProgressChangeResult {
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 123;
  int spineIndex = 2, page = 5, totalPages = 20;
  bool hasSavedProgress = false;
  std::string xpath = "/p";
  float percentage = .5f;
};
struct Renderer {};
namespace ProgressMapper {
CrossPointPosition toCrossPoint(const std::shared_ptr<Epub>&, SavedProgressPosition value, Renderer&, int spine,
                                int count) {
  std::printf("fallback [%s] %.9g %d %d\n", value.xpath.c_str(), value.percentage, spine, count);
  return {4, 9};
}
}  // namespace ProgressMapper
namespace EpubReaderUtils {
bool saveProgress(const Epub&, int spine, int page, int count, std::optional<uint32_t> offset) {
  std::printf("save %d %d %d %d %u\n", spine, page, count, bool(offset), offset.value_or(0));
  return true;
}
}  // namespace EpubReaderUtils
int clampPercent(int value) { return std::clamp(value, 0, 100); }
struct EpubReaderActivity {
  struct ModelLock {
    explicit ModelLock(EpubReaderActivity&) {}
  };
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  Renderer renderer;
  int currentSpineIndex = 2, cachedSpineIndex = 2, nextPageNumber = 3;
  int cachedChapterTotalPageCount = 10;
  std::optional<uint32_t> cachedVisibleTextOffset = 123, currentPageVisibleOffset = 456, pendingOffsetJump;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0;
  std::string pendingAnchor = "old";
  struct SavedPosition {
    int spineIndex, pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH]{{0, 1}, {1, 2}, {2, 3}};
  int footnoteDepth = 0;
  void requestUpdate() { event("update"); }
  bool runNavigationPolicy(uint8_t operation, int32_t a = 0, int32_t b = 0, int32_t c = 0,
                           const std::string* href = nullptr, const ProgressChangeResult* sync = nullptr);
  void jumpToPercent(int percent);
  void applyProgressChange(const ProgressChangeResult& sync);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  bool saveProgress(int spine, int page, int count);
  void rememberCurrentContentOffset();
  void navigateToHref(const std::string&, bool savePosition);
  void restoreSavedPosition();
  void dump() const {
    std::printf("state %d %d %d %d %u %d %u %d %.9g [%s] %d", currentSpineIndex, nextPageNumber,
                section ? section->currentPage : -999, cachedChapterTotalPageCount, cachedVisibleTextOffset.value_or(0),
                bool(pendingOffsetJump), pendingOffsetJump.value_or(0), pendingPercentJump, pendingSpineProgress,
                pendingAnchor.c_str(), footnoteDepth);
    for (const auto& p : savedPositions) std::printf(" %d:%d", p.spineIndex, p.pageNumber);
    std::printf(" cached=%d\n", bool(cachedVisibleTextOffset));
    assert(!locks);
  }
};
// C3 size_t arithmetic, including wrapping subtraction before a float conversion.
#define size_t uint32_t
// NAVIGATION_METHODS
#undef size_t
int main() {
  unsigned id = 0;
  for (uint32_t size : {0u, 1u, 99u, 100u, 101u, UINT32_MAX})
    for (int count : {0, 1, 7})
      for (int percent : {-1, 0, 1, 33, 99, 100, 101})
        for (int found : {-1, 0, 2}) {
          EpubReaderActivity a;
          a.epub->size = size;
          a.epub->cumulative = size;
          a.epub->previous = size / 4;
          a.epub->spineCount = count;
          a.epub->found = found;
          std::printf("percent %u\n", id++);
          a.jumpToPercent(percent);
          a.dump();
        }
  for (int flags = 0; flags < 128; ++flags)
    for (int page : {-1, 0, 3, 19, 30}) {
      EpubReaderActivity a;
      a.currentSpineIndex = flags & 1 ? 2 : 3;
      a.section->building = flags & 2;
      if (flags & 4) a.cachedVisibleTextOffset.reset();
      a.cachedChapterTotalPageCount = flags & 8 ? 20 : (flags & 16 ? 0 : 10);
      if (flags & 32) a.section->offsetPage = 7;
      a.section->currentPage = page;
      a.section->pageCount = flags & 64 ? 0 : 20;
      std::printf("deferred %u\n", id++);
      const bool changed = a.applyDeferredReposition();
      std::printf("changed %d\n", changed);
      a.dump();
    }
  for (int flags = 0; flags < 64; ++flags)
    for (int page : {-1, 0, 3, 19, 30}) {
      EpubReaderActivity a;
      ProgressChangeResult result;
      result.hasVisibleTextOffset = flags & 1;
      result.hasSavedProgress = flags & 2;
      result.spineIndex = flags & 4 ? 2 : (flags & 8 ? -1 : 4);
      result.page = page;
      result.totalPages = flags & 16 ? 20 : 0;
      if (flags & 32) a.section->offsetPage = 7;
      std::printf("progress %u\n", id++);
      a.applyProgressChange(result);
      a.dump();
    }
  for (int flags = 0; flags < 16; ++flags)
    for (int depth = 0; depth <= 3; ++depth)
      for (const char* href : {"", "x", "#a", "x#a", "#", "a#b#c"}) {
        EpubReaderActivity a;
        a.footnoteDepth = depth;
        if (flags & 1) a.section.reset();
        if (flags & 2) a.epub->resolved = -1;
        if (flags & 4) a.epub.reset();
        std::printf("href %u\n", id++);
        a.navigateToHref(href, flags & 8);
        a.dump();
        a.restoreSavedPosition();
        a.dump();
      }
  for (int flags = 0; flags < 32; ++flags)
    for (int page : {-1, 0, 3, 19, 20}) {
      EpubReaderActivity a;
      if (flags & 1) a.section->offset = 111;
      if (flags & 2) a.currentPageVisibleOffset.reset();
      if (flags & 4) a.section.reset();
      std::printf("offset %u\n", id++);
      a.saveProgress(flags & 8 ? 2 : 3, page, flags & 16 ? 0 : 20);
      a.rememberCurrentContentOffset();
      a.dump();
    }
}
