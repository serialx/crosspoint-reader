#pragma once

#include <BufferedFile.h>
#include <HalStorage.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class BookMetadataCache {
 public:
  struct BookMetadata {
    std::string title;
    std::string author;
    std::string language;
    std::string coverItemHref;
    std::string textReferenceHref;
  };

  struct SpineEntry {
    std::string href;
    uint32_t cumulativeSize;
    int16_t tocIndex;

    SpineEntry() : cumulativeSize(0), tocIndex(-1) {}
    SpineEntry(std::string href, const uint32_t cumulativeSize, const int16_t tocIndex)
        : href(std::move(href)), cumulativeSize(cumulativeSize), tocIndex(tocIndex) {}
  };

  struct TocEntry {
    std::string title;
    std::string href;
    std::string anchor;
    uint8_t level;
    int16_t spineIndex;

    TocEntry() : level(0), spineIndex(-1) {}
    TocEntry(std::string title, std::string href, std::string anchor, const uint8_t level, const int16_t spineIndex)
        : title(std::move(title)),
          href(std::move(href)),
          anchor(std::move(anchor)),
          level(level),
          spineIndex(spineIndex) {}
  };

 private:
  std::string cachePath;
  uint32_t lutOffset;
  uint16_t spineCount;
  uint16_t tocCount;
  bool loaded;
  XpPkgCache* build = nullptr;
  struct BuildStreams;
  struct ReadContext;
  HalFile& fileFor(uint32_t file);
  static uint32_t control(void*, uint32_t, uint32_t);
  static size_t read(void*, uint32_t, uint8_t*, size_t);
  static size_t write(void*, uint32_t, const uint8_t*, size_t);
  static uint8_t seek(void*, uint32_t, uint32_t);
  static void observe(void*, uint32_t, XpPkgSpan, XpPkgSpan, XpPkgSpan, uint8_t);

  HalFile bookFile;
  // Temp file handles during build
  HalFile spineFile;
  HalFile tocFile;
  // Buffers the per-entry tmp-file writes during the OPF/TOC passes: those
  // writes interleave with zip-inflate SD reads, and unbuffered they thrash
  // SdFat's shared sector cache (one 512B transaction per 4-byte pod). One
  // wrapper serves whichever pass is active (spine, then toc).
  std::unique_ptr<serialization::BufferedFileWriter> passOut;

  // Cumulative spine sizes stay in book.bin; RAM holds every SIZE_WINDOW-th one and one window.
  static constexpr int SIZE_WINDOW = XP_BOOK_METADATA_WINDOW;
  std::vector<uint32_t> windowStarts;  // total before item w * SIZE_WINDOW
  uint32_t bookSize = 0;
  mutable int windowFirst = -1;
  mutable uint32_t window[SIZE_WINDOW] = {};
  static_assert(sizeof(window) / sizeof(window[0]) == XP_BOOK_METADATA_WINDOW);

  SpineEntry readSpineEntry(HalFile& file) const;
  TocEntry readTocEntry(HalFile& file) const;

 public:
  BookMetadata coreMetadata;

  explicit BookMetadataCache(std::string cachePath)
      : cachePath(std::move(cachePath)), lutOffset(0), spineCount(0), tocCount(0), loaded(false) {}
  ~BookMetadataCache();
  XpPkgCache* writer() const { return build; }
  XpPkgIo sink();

  // Building phase (stream to disk immediately)
  bool beginWrite();
  bool beginContentOpfPass();
  void createSpineEntry(const std::string& href);
  bool endContentOpfPass();
  bool beginTocPass();
  bool createTocEntry(std::string_view title, std::string_view href, std::string_view anchor, uint8_t level);
  bool endTocPass();
  bool endWrite();
  bool cleanupTmpFiles() const;

  // Post-processing to update mappings and sizes
  bool buildBookBin(const std::string& epubPath, const BookMetadata& metadata);

  // Reading phase (read mode)
  bool load();
  SpineEntry getSpineEntry(int index);
  TocEntry getTocEntry(int index);
  // Cumulative byte size up to and including the given spine item (0 if out of range
  // or not loaded).
  uint32_t getCumulativeSize(int index) const;
  // First spine index whose cumulative size reaches `size` (-1 past the end or not loaded). Reads at most one window.
  int getSpineIndexForSize(uint32_t size) const;
  int getSpineCount() const { return build ? xp_pkg_cache_count(build, 0) : spineCount; }
  int getTocCount() const { return build ? xp_pkg_cache_count(build, 1) : tocCount; }
  bool isLoaded() const { return loaded; }
};
