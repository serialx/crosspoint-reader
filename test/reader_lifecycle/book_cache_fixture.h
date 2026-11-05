#include <Memory.h>
#include <xp_epub_session.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>

#define HalFile HostFile
#include <HalStorage.h>
#undef HalFile
#include "physical_defaults.h"

static int filesBorn = 0, filesDied = 0;
struct HalFile : HostFile {
  HalFile() { ++filesBorn; }
  ~HalFile() { ++filesDied; }
};
static thread_local int cacheDepth = 0;
static std::atomic<int> cacheAttempts{0};
struct CacheMutex {
  std::mutex mutex;
  void lock() {
    assert(cacheDepth == 0);
    ++cacheAttempts;
    mutex.lock();
    ++cacheDepth;
  }
  void unlock() {
    assert(cacheDepth == 1);
    --cacheDepth;
    mutex.unlock();
  }
} sizeWindowLock;

extern "C" {
using CacheSink = void (*)(void*, uint32_t, const uint8_t*, size_t, int64_t);
void* xp_epub_test_cache_create(const XpEpubLeaves*);
void xp_epub_test_cache_destroy(void*);
uint8_t xp_epub_test_cache_set(void*, uint32_t, const uint8_t*, size_t);
uint8_t xp_epub_test_cache_load(void*, const uint8_t*, size_t);
int64_t xp_epub_test_cache_query(const void*, uint32_t, int32_t, CacheSink, void*);
size_t xp_test_live_allocations();
void xp_test_fail_allocation_after(size_t);
}
static const XpEpubLeaves& cacheLeaves();

// Test-only representation adapter for the unchanged native read oracle.
class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title, author, language, coverItemHref, textReferenceHref;
  } coreMetadata;
  struct SpineEntry {
    std::string href;
    uint32_t cumulativeSize = 0;
    int16_t tocIndex = -1;
  };
  struct TocEntry {
    std::string title, href, anchor;
    uint8_t level = 0;
    int16_t spineIndex = -1;
  };
  static void lockReadCursor();
  static void unlockReadCursor();
  explicit BookMetadataCache(const std::string& path)
      : path(path + "/book.bin"), owner(xp_epub_test_cache_create(&cacheLeaves())) {
    assert(owner);
  }
  ~BookMetadataCache() { xp_epub_test_cache_destroy(owner); }
  BookMetadataCache(const BookMetadataCache&) = delete;
  bool load() {
    const std::string* fields[] = {&coreMetadata.title, &coreMetadata.author, &coreMetadata.language,
                                   &coreMetadata.coverItemHref, &coreMetadata.textReferenceHref};
    for (uint32_t i = 0; i < 5; ++i)
      assert(xp_epub_test_cache_set(owner, i, reinterpret_cast<const uint8_t*>(fields[i]->data()), fields[i]->size()));
    const bool ok = xp_epub_test_cache_load(owner, reinterpret_cast<const uint8_t*>(path.data()), path.size());
    loaded = xp_epub_test_cache_query(
        owner, 0, 0,
        [](void* raw, uint32_t field, const uint8_t* bytes, size_t size, int64_t number) {
          assert(cacheDepth == 0);
          auto& self = *static_cast<BookMetadataCache*>(raw);
          std::string* fields[] = {&self.coreMetadata.title, &self.coreMetadata.author, &self.coreMetadata.language,
                                   &self.coreMetadata.coverItemHref, &self.coreMetadata.textReferenceHref};
          if (field < 5)
            fields[field]->assign(reinterpret_cast<const char*>(bytes), size);
          else if (field == 5)
            self.spines = number;
          else
            self.toc = number;
        },
        this);
    return ok;
  }
  bool isLoaded() const { return loaded; }
  uint16_t getSpineCount() const { return spines; }
  uint16_t getTocCount() const { return toc; }
  SpineEntry getSpineEntry(int index) const {
    SpineEntry row;
    xp_epub_test_cache_query(
        owner, 1, index,
        [](void* raw, uint32_t field, const uint8_t* bytes, size_t size, int64_t number) {
          assert(cacheDepth == 0);
          auto& row = *static_cast<SpineEntry*>(raw);
          if (field == 0)
            row.href.assign(reinterpret_cast<const char*>(bytes), size);
          else if (field == 1)
            row.cumulativeSize = number;
          else
            row.tocIndex = number;
        },
        &row);
    return row;
  }
  TocEntry getTocEntry(int index) const {
    TocEntry row;
    xp_epub_test_cache_query(
        owner, 2, index,
        [](void* raw, uint32_t field, const uint8_t* bytes, size_t size, int64_t number) {
          assert(cacheDepth == 0);
          auto& row = *static_cast<TocEntry*>(raw);
          std::string* fields[] = {&row.title, &row.href, &row.anchor};
          if (field < 3)
            fields[field]->assign(reinterpret_cast<const char*>(bytes), size);
          else if (field == 3)
            row.level = number;
          else
            row.spineIndex = number;
        },
        &row);
    return row;
  }
  uint32_t getCumulativeSize(int index) const { return xp_epub_test_cache_query(owner, 3, index, nullptr, nullptr); }
  int getSpineIndexForSize(uint32_t size) const {
    return xp_epub_test_cache_query(owner, 4, static_cast<int32_t>(size), nullptr, nullptr);
  }
  const void* testHandle() const { return owner; }

 private:
  std::string path;
  void* owner;
  bool loaded = false;
  uint16_t spines = 0, toc = 0;
};

// ACTUAL_TEXT_HELPERS
// ACTUAL_MUTEX_METHODS
static const XpEpubLeaves& cacheLeaves() {
  static const XpEpubLeaves leaves = [] {
    XpEpubLeaves value = unexpectedEpubLeaves();
    value.log_enabled = [](uint8_t) -> uint8_t { return 0; };
    // ACTUAL_LEAVES
    return value;
  }();
  return leaves;
}
