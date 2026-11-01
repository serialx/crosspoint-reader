#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

#include "session_fixture.h"

static unsigned locks, modelLocks, liveSections;
static bool publishing;
static unsigned publicationStep;
static void leaf() { assert(!modelLocks); }
static void event(const char* text, int a = 0, int b = 0) {
  leaf();
  std::printf("%s %d %d lock=%u\n", text, a, b, locks);
}
static void publicationRead(unsigned step) {
  leaf();
  if (publishing) assert(publicationStep++ == step);
}
struct RenderLock {
  RenderLock() {
    leaf();
    assert(!locks);
    ++locks;
    event("lock");
  }
  ~RenderLock() {
    event("unlock");
    assert(locks == 1);
    --locks;
  }
};
struct Section {
  uint16_t pageCount = 20;
  bool building = false;
  std::optional<uint16_t> offsetPage;
  std::optional<uint32_t> offset;
  Section() { assert(liveSections++ == 0); }
  ~Section() {
    leaf();
    assert(liveSections-- == 1);
    event("drop-section");
  }
  bool isBuilding() const {
    if (!publishing) event("building", building);
    return building;
  }
  uint16_t estimatedTotalPages() const {
    if (!publishing) event("page-count", pageCount);
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
  int resolveHrefToSpineIndex(XpAppText href) const {
    leaf();
    std::printf("resolve [%.*s] %d\n", int(href.len), reinterpret_cast<const char*>(href.data), resolved);
    return resolved;
  }
};
struct ProgressChangeResult {
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 123;
  int spineIndex = 2, page = 5, totalPages = 20;
  bool hasSavedProgress = false;
  std::string xpath = "/p";
  float percentage = .5f;
};
static XpAppText text(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

// These members are construction inputs only; every post-call observation is Rust-owned.
struct NavigationFixture {
  std::unique_ptr<Epub> epub = std::make_unique<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  int currentSpineIndex = 2, cachedSpineIndex = 2, nextPageNumber = 3, activePage = 3;
  int cachedChapterTotalPageCount = 10;
  std::optional<uint32_t> cachedVisibleTextOffset = 123, currentPageVisibleOffset = 456;
  std::string pendingAnchor = "old";
  int footnoteDepth = 0;
  void* owner = nullptr;
  ~NavigationFixture();
  void initialize();
  void jumpToPercent(int percent);
  void applyProgressChange(const ProgressChangeResult& sync);
  bool applyDeferredReposition();
  void clearDeferredReposition();
  bool saveProgress(int spine, int page, int count);
  void rememberCurrentContentOffset();
  void navigateToHref(const std::string&, bool savePosition);
  void restoreSavedPosition();
  void dump();
};
#include "navigation_rust.inc"
int main() {
  unsigned id = 0;
  for (uint32_t size : {0u, 1u, 99u, 100u, 101u, UINT32_MAX})
    for (int count : {0, 1, 7})
      for (int percent : {-1, 0, 1, 33, 99, 100, 101})
        for (int found : {-1, 0, 2}) {
          NavigationFixture a;
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
      NavigationFixture a;
      a.currentSpineIndex = flags & 1 ? 2 : 3;
      a.section->building = flags & 2;
      if (flags & 4) a.cachedVisibleTextOffset.reset();
      a.cachedChapterTotalPageCount = flags & 8 ? 20 : (flags & 16 ? 0 : 10);
      if (flags & 32) a.section->offsetPage = 7;
      a.activePage = page;
      a.section->pageCount = flags & 64 ? 0 : 20;
      std::printf("deferred %u\n", id++);
      const bool changed = a.applyDeferredReposition();
      std::printf("changed %d\n", changed);
      a.dump();
    }
  for (int flags = 0; flags < 64; ++flags)
    for (int page : {-1, 0, 3, 19, 30}) {
      NavigationFixture a;
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
        NavigationFixture a;
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
      NavigationFixture a;
      if (flags & 1) a.section->offset = 111;
      if (flags & 2) a.currentPageVisibleOffset.reset();
      if (flags & 4) a.section.reset();
      std::printf("offset %u\n", id++);
      a.saveProgress(flags & 8 ? 2 : 3, page, flags & 16 ? 0 : 20);
      a.rememberCurrentContentOffset();
      a.dump();
    }
}
