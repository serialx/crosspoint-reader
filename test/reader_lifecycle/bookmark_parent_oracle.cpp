#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BookmarkEntry.h"
#include "HalStorage.h"
#include "bookmark_session_fixture.h"
static unsigned modelDepth;
static bool publishing;
static unsigned publicationStep;
static unsigned sectionDrops, textReads, offsetReads;
static void leaf() { assert(modelDepth == 0); }
static void event(const char* name, int a = 0, int b = 0) {
  leaf();
  std::printf("%s %d %d\n", name, a, b);
}
static uint32_t bits(float f) {
  uint32_t n;
  std::memcpy(&n, &f, 4);
  return n;
}
static int lockDepth = 0;
struct RenderLock {
  RenderLock() {
    leaf();
    assert(lockDepth == 0);
    event("lock", ++lockDepth);
  }
  ~RenderLock() {
    assert(lockDepth == 1);
    event("unlock", lockDepth--);
  }
};
#ifdef BOOKMARK_RELEASE_LOG
#define LOG_DBG(...) ((void)0)
#else
#define LOG_DBG(tag, fmt, ...) std::printf("debug " fmt "\n", ##__VA_ARGS__)
#endif
#define LOG_ERR(tag, fmt, ...) std::printf("error " fmt "\n", ##__VA_ARGS__)
struct Epub {
  int count = 4;
  std::string getPath() const {
    event("path");
    return "/fixture.epub";
  }
  std::string getCachePath() const { return "/cache"; }
  int getSpineItemsCount() const { return count; }
  float calculateProgress(int spine, float fraction) {
    std::printf("progress %d %08x\n", spine, bits(fraction));
    return (spine + fraction) / 4.0f;
  }
};
struct Section;
static Section* liveSection;
struct Section {
  Section() {
    assert(!liveSection);
    liveSection = this;
  }
  ~Section() {
    assert(liveSection == this);
    liveSection = nullptr;
    ++sectionDrops;
  }
  uint16_t pageCount = 5;
  int estimated = 7;
  std::optional<uint32_t> offset = 123;
  int estimatedTotalPages() {
    if (!publishing) event("estimate", estimated, lockDepth);
    return estimated;
  }
  std::string getTextFromSectionFile(int page) {
    ++textReads;
    event("text", page, lockDepth);
    return "A sufficiently long bookmark summary from page text.";
  }
  std::optional<uint32_t> getVisibleTextOffsetForPage(uint16_t page) {
    ++offsetReads;
    event("offset", page, lockDepth);
    return offset;
  }
};
struct CrossPointPosition {};
struct SavedProgressPosition {
  std::string xpath;
  float percentage;
};
namespace ProgressMapper {
SavedProgressPosition toSavedProgress(const Epub&, const CrossPointPosition&) {
  event("map", lockDepth);
  return {"/body/long/path/to/visible/text", 0.375f};
}
}  // namespace ProgressMapper
namespace BookmarkUtil {
void observeSummary(const std::string& text) { std::printf("sanitize [%s]\n", text.c_str()); }
}  // namespace BookmarkUtil
static bool saveOk = true;
static unsigned saveCalls;
static std::vector<BookmarkEntry> stored;
static void dump(const std::vector<BookmarkEntry>& entries) {
  event("entries", entries.size());
  for (const auto& b : entries)
    std::printf("entry %08x [%s] [%s] %d %d %d %u %d\n", bits(b.percentage), b.xpath.c_str(), b.summary.c_str(),
                b.computedSpineIndex, b.computedChapterProgress, b.computedChapterPageCount, b.visibleTextOffset,
                b.hasVisibleTextOffset);
}
namespace BookmarkFile {
bool load(const std::string& path, std::vector<BookmarkEntry>& entries) {
  std::printf("load [%s]\n", path.c_str());
  entries = stored;
  return !entries.empty();
}
bool save(const std::string& path, const std::vector<BookmarkEntry>& entries) {
  ++saveCalls;
  std::printf("save [%s] %d\n", path.c_str(), saveOk);
  dump(entries);
  return saveOk;
}
}  // namespace BookmarkFile
struct ProgressRange {
  float start, end;
};
struct BookmarkFixture {
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  std::unique_ptr<Section> section = std::make_unique<Section>();
  std::vector<BookmarkEntry> cachedBookmarks;
  int currentSpineIndex = 1, activePage = 2;
  bool bookmarkRemoved = false, currentPageBookmarked = false;
  std::optional<uint32_t> currentPageVisibleOffset;
  struct SavedPosition {
    int spineIndex, pageNumber;
  };
  SavedPosition savedPositions[3]{};
  int footnoteDepth = 0;
  void* owner = nullptr;
  ~BookmarkFixture();
  void initialize();
  XpEpubTestBookmarkObservation observe();
  void seedHistory();
  void loadCachedBookmarks();
  void addBookmark();
  void updateBookmarkFlag();
  void saveLinkStack();
  void loadLinkStack();
};
static BookmarkFixture* activeFixture;
static bool dropAfterUnlock;
#include "bookmark_session_rust.inc"
static BookmarkEntry entry(float percent, int spine, int page, int count) {
  BookmarkEntry e;
  e.percentage = percent;
  e.xpath = "/body/bookmark/source/long-path";
  e.summary = "Existing sufficiently long saved bookmark summary.";
  e.computedSpineIndex = spine;
  e.computedChapterProgress = page;
  e.computedChapterPageCount = count;
  return e;
}
static void sectionDropCase() {
  BookmarkFixture o;
  saveOk = true;
  stored.clear();
  o.initialize();
  const auto drops = sectionDrops, texts = textReads, offsets = offsetReads, saves = saveCalls;
  dropAfterUnlock = true;
  o.addBookmark();
  const auto state = o.observe();
  assert(!dropAfterUnlock && !state.has_section && sectionDrops == drops + 1);
  assert(textReads == texts && offsetReads == offsets);
  assert(o.cachedBookmarks.size() == 1 && o.cachedBookmarks[0].summary.empty());
  assert(!o.cachedBookmarks[0].hasVisibleTextOffset && state.bookmarked && !state.removed);
  assert(saveCalls == saves + 1);
  std::puts("section-drop summary-empty offset-absent saved=1");
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--section-drop") {
    sectionDropCase();
    return 0;
  }
  assert(argc == 1);
  for (int flags = 0; flags < 32; ++flags)
    for (int page : {-1, 0, 2, 4, 7})
      for (int count : {0, 1, 5, 7})
        for (int set = 0; set < 4; ++set) {
          std::printf("case %d %d %d %d\n", flags, page, count, set);
          BookmarkFixture o;
          o.activePage = page;
          o.section->estimated = count;
          if (flags & 1) o.section.reset();
          if (flags & 2) o.epub.reset();
          if (flags & 4) o.currentPageVisibleOffset = 987;
          if (o.section && (flags & 8)) o.section->offset.reset();
          saveOk = flags & 16;
          stored.clear();
          if (set == 1) stored = {entry(0.1f, 1, page, count), entry(0.9f, 3, 8, 12)};
          if (set == 2) stored = {entry(0.375f, 3, 8, 12), entry(0.1f, 0, 8, 12), entry(0.375f, 3, 8, 12)};
          if (set == 3) stored = {entry(0.0f, 0, 0, 1), entry(0.5f, 2, 0, 1), entry(1.0f, 3, 1, 2)};
          o.cachedBookmarks = stored;
          o.currentPageBookmarked = true;
          o.updateBookmarkFlag();
          event("flag", o.observe().bookmarked);
          o.addBookmark();
          dump(o.cachedBookmarks);
          const auto result = o.observe();
          event("result", result.removed, result.bookmarked);
          o.loadCachedBookmarks();
          dump(o.cachedBookmarks);
          event("loaded", o.observe().bookmarked);
          assert(lockDepth == 0);
        }
  for (int count : {-1, 0, 1, 2, 5, 37})
    for (int page : {-1, 0, 1, 4, 36, 100}) {
      auto epub = std::make_shared<Epub>();
      auto range = getPageProgressRange(epub, 1, page, count);
      std::printf("range %d %d %08x %08x\n", count, page, bits(range.start), bits(range.end));
      for (float value : {-1.0f, 0.0f, 0.2499f, 0.25f, 0.2501f, 0.375f, 0.4999f, 0.5f, 0.5001f, 1.0f, 2.0f, NAN})
        for (int match : {0, 1})
          event("matches", bookmarkMatchesProgress(entry(value, match ? 1 : 3, match ? page : 123, match ? count : 456),
                                                   1, page, count, range));
    }
  for (float start : {-INFINITY, -0.0f, 0.0f, 0.2499f, 0.25f, 0.2501f, INFINITY, NAN})
    for (float end : {-INFINITY, -0.0f, 0.0f, 0.4999f, 0.5f, 0.5001f, INFINITY, NAN})
      for (float value : {-INFINITY, -0.0f, 0.0f, 0.2499f, 0.25f, 0.2501f, 0.4999f, 0.5f, 0.5001f, INFINITY, NAN}) {
        const ProgressRange range{start, end};
        std::printf("edge %08x %08x %08x %d\n", bits(start), bits(end), bits(value),
                    bookmarkMatchesProgress(entry(value, 3, 123, 456), 1, 2, 5, range));
      }
  storageTrace.trackLifetime = true;
  for (int depth = 0; depth <= 3; ++depth)
    for (bool open : {false, true})
      for (size_t limit : {0u, 1u, 5u, 13u}) {
        storageTrace.files.clear();
        storageTrace.openOk = open;
        storageTrace.writeLimit = limit;
        BookmarkFixture o;
        o.footnoteDepth = depth;
        for (int i = 0; i < 3; ++i) o.savedPositions[i] = {i, 1000 * i};
        o.saveLinkStack();
        o.footnoteDepth = 0;
        for (auto& p : o.savedPositions) p = {17, 19};
        o.seedHistory();
        o.loadLinkStack();
        const auto history = o.observe();
        event("depth", history.history_depth);
        for (int i = 0; i < 3; ++i) event("position", history.history[2 * i], history.history[2 * i + 1]);
        assert(storageTrace.liveFiles == 0);
      }
  for (int size = 0; size < 15; ++size)
    for (int depth : {0, 1, 2, 3, 4, 255})
      for (int spine : {0, 3, 4, 65535}) {
        storageTrace.files.clear();
        storageTrace.openOk = true;
        std::vector<uint8_t> data(size, 0);
        if (size) data[0] = depth;
        if (size > 2) {
          data[1] = spine & 255;
          data[2] = spine >> 8;
        }
        storageTrace.files["/cache/links.bin"] = data;
        BookmarkFixture o;
        o.footnoteDepth = 2;
        for (auto& p : o.savedPositions) p = {17, 19};
        o.loadLinkStack();
        const auto history = o.observe();
        event("depth", history.history_depth);
        for (int i = 0; i < 3; ++i) event("position", history.history[2 * i], history.history[2 * i + 1]);
        assert(storageTrace.liveFiles == 0);
      }
}
