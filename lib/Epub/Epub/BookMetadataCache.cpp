#include "BookMetadataCache.h"

#include <BufferedFile.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>
#include <xp_ffi.h>

#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <optional>

#include "FsHelpers.h"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 10;  // v10: ignore ambiguous guide text references
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
// Buffer size for the buildBookBin streams. 3 buffers x 4KB, transient (freed on
// return); 4KB = 8 SD sectors per transfer, enough to stop the sector-cache thrash.
constexpr size_t BUILD_IO_BUFFER_SIZE = 4096;
// The render task and the main loop both ask for sizes.
std::mutex sizeWindowLock;

template <typename F>
BookMetadataCache::SpineEntry readSpineEntryFrom(F& file) {
  BookMetadataCache::SpineEntry entry;
  serialization::readString(file, entry.href);
  serialization::readPod(file, entry.cumulativeSize);
  serialization::readPod(file, entry.tocIndex);
  return entry;
}

template <typename F>
BookMetadataCache::TocEntry readTocEntryFrom(F& file) {
  BookMetadataCache::TocEntry entry;
  serialization::readString(file, entry.title);
  serialization::readString(file, entry.href);
  serialization::readString(file, entry.anchor);
  serialization::readPod(file, entry.level);
  serialization::readPod(file, entry.spineIndex);
  return entry;
}
}  // namespace

struct BookMetadataCache::BuildStreams {
  BookMetadataCache& owner;
  const std::string& path;
  std::optional<serialization::BufferedFileWriter> book;
  std::optional<serialization::BufferedFileReader> spine;
  std::optional<serialization::BufferedFileReader> toc;
  std::optional<ZipFile> zip;
  std::optional<std::deque<uint32_t>> sizes;
  size_t rawSize = 0;
  bool isZip;
  explicit BuildStreams(BookMetadataCache& owner, const std::string& path)
      : owner(owner), path(path), isZip(!FsHelpers::hasTxtExtension(path) && !FsHelpers::hasMarkdownExtension(path)) {}
  bool beginSizes() {
    if (!isZip) {
      HalFile raw;
      if (Storage.openFileForRead("BMC", path, raw)) rawSize = raw.size();
      return true;
    }
    zip.emplace(path);
    if (!zip->open()) {
      LOG_ERR("BMC", "Could not open EPUB for sizes");
      return false;
    }
    if (owner.spineCount >= 400) {
      std::deque<ZipFile::SizeTarget> targets;
      sizes.emplace(owner.spineCount, 0);
      spine->seek(0);
      for (int i = 0; i < owner.spineCount; ++i) {
        const auto entry = readSpineEntryFrom(*spine);
        const auto name = FsHelpers::normalisePath(entry.href);
        targets.push_back({ZipFile::fnvHash64(name.c_str(), name.size()), static_cast<uint16_t>(name.size()),
                           static_cast<uint16_t>(i)});
        if (targets.size() < 2048 && i + 1 < owner.spineCount) continue;
        std::sort(targets.begin(), targets.end(),
                  [](const auto& a, const auto& b) { return a.hash < b.hash || (a.hash == b.hash && a.len < b.len); });
        zip->fillUncompressedSizes(targets, *sizes);
        targets.clear();
      }
    }
    return true;
  }
  XpPkgIo sink() { return {this, control, read, write, seek, itemSize, nullptr}; }
  static uint32_t control(void* context, uint32_t id, uint32_t operation) {
    auto& self = *static_cast<BuildStreams*>(context);
    switch (operation) {
      case XP_PKG_BEGIN_BUILD:
        self.book.emplace(self.owner.bookFile, BUILD_IO_BUFFER_SIZE);
        self.spine.emplace(self.owner.spineFile, BUILD_IO_BUFFER_SIZE);
        self.toc.emplace(self.owner.tocFile, BUILD_IO_BUFFER_SIZE);
        return 1;
      case XP_PKG_END_BUILD:
        return self.book->flush();
      case XP_PKG_BEGIN_SIZES:
        return self.beginSizes();
      case XP_PKG_END_SIZES:
        self.sizes.reset();
        if (self.zip) self.zip->close();
        return 1;
      case XP_PKG_POSITION:
        if (id == XP_PKG_SPINE_FILE) return self.spine->position();
        if (id == XP_PKG_TOC_FILE) return self.toc->position();
        return self.book->position();
      default:
        return BookMetadataCache::control(&self.owner, id, operation);
    }
  }
  static size_t read(void* context, uint32_t id, uint8_t* bytes, size_t size) {
    auto& self = *static_cast<BuildStreams*>(context);
    return id == XP_PKG_SPINE_FILE ? self.spine->read(bytes, size) : self.toc->read(bytes, size);
  }
  static size_t write(void* context, uint32_t, const uint8_t* bytes, size_t size) {
    static_cast<BuildStreams*>(context)->book->write(bytes, size);
    return size;
  }
  static uint8_t seek(void* context, uint32_t id, uint32_t offset) {
    auto& self = *static_cast<BuildStreams*>(context);
    return id == XP_PKG_SPINE_FILE ? self.spine->seek(offset) : self.toc->seek(offset);
  }
  static uint32_t itemSize(void* context, uint16_t index, XpPkgSpan href) {
    auto& state = *static_cast<BuildStreams*>(context);
    if (!state.isZip) return state.rawSize;
    size_t size = state.sizes ? (*state.sizes)[index] : 0;
    if (size == 0) {
      const auto path = FsHelpers::normalisePath(std::string_view(reinterpret_cast<const char*>(href.data), href.len));
      if (!state.zip->getInflatedFileSize(path.c_str(), &size))
        LOG_ERR("BMC", "Could not get size for %s", path.c_str());
    }
    return size;
  }
};

BookMetadataCache::~BookMetadataCache() { xp_pkg_cache_destroy(build); }
HalFile& BookMetadataCache::fileFor(uint32_t file) {
  if (file == XP_PKG_SPINE_FILE) return spineFile;
  if (file == XP_PKG_TOC_FILE) return tocFile;
  return bookFile;
}
uint32_t BookMetadataCache::control(void* context, uint32_t id, uint32_t operation) {
  auto& self = *static_cast<BookMetadataCache*>(context);
  auto& file = self.fileFor(id);
  const char* suffix = id == XP_PKG_SPINE_FILE ? tmpSpineBinFile : id == XP_PKG_TOC_FILE ? tmpTocBinFile : bookBinFile;
  switch (operation) {
    case XP_PKG_OPEN_READ:
      return Storage.openFileForRead("BMC", self.cachePath + suffix, file);
    case XP_PKG_OPEN_WRITE:
      return Storage.openFileForWrite("BMC", self.cachePath + suffix, file);
    case XP_PKG_CLOSE:
      file.close();
      return 1;
    case XP_PKG_REMOVE:
      return Storage.remove((self.cachePath + suffix).c_str());
    case XP_PKG_IS_OPEN:
      return static_cast<bool>(file);
    case XP_PKG_POSITION:
      return file.position();
    case XP_PKG_SIZE:
      return file.size();
    case XP_PKG_BEGIN_PASS:
      self.passOut = makeUniqueNoThrow<serialization::BufferedFileWriter>(file, BUILD_IO_BUFFER_SIZE);
      return 1;
    case XP_PKG_END_PASS: {
      const bool ok = !self.passOut || self.passOut->flush();
      self.passOut.reset();
      return ok;
    }
    default:
      return 0;
  }
}
size_t BookMetadataCache::read(void* context, uint32_t id, uint8_t* bytes, size_t size) {
  auto& self = *static_cast<BookMetadataCache*>(context);
  const int count = self.fileFor(id).read(bytes, size);
  return count < 0 ? 0 : static_cast<size_t>(count);
}
size_t BookMetadataCache::write(void* context, uint32_t id, const uint8_t* bytes, size_t size) {
  auto& self = *static_cast<BookMetadataCache*>(context);
  if (self.passOut) {
    self.passOut->write(bytes, size);
    return size;
  }
  return self.fileFor(id).write(bytes, size);
}
uint8_t BookMetadataCache::seek(void* context, uint32_t id, uint32_t offset) {
  auto& self = *static_cast<BookMetadataCache*>(context);
  return self.fileFor(id).seek(offset);
}
void BookMetadataCache::observe(void*, uint32_t, XpPkgSpan, XpPkgSpan, XpPkgSpan, uint8_t) {}
XpPkgIo BookMetadataCache::sink() { return {this, control, read, write, seek, nullptr, nullptr}; }

bool BookMetadataCache::beginWrite() {
  xp_pkg_cache_destroy(build);
  build = nullptr;
  spineCount = tocCount = 0;
  if (xp_pkg_cache_create(&build) != 0) {
    LOG_ERR("BMC", "Could not allocate cache writer");
    return false;
  }
  return true;
}
bool BookMetadataCache::beginContentOpfPass() {
  const auto io = sink();
  return xp_pkg_cache_control(build, &io, XP_PKG_BEGIN_OPF) == 0;
}
bool BookMetadataCache::endContentOpfPass() {
  const auto io = sink();
  return xp_pkg_cache_control(build, &io, XP_PKG_END_OPF) == 0;
}
bool BookMetadataCache::beginTocPass() {
  const auto io = sink();
  return xp_pkg_cache_control(build, &io, XP_PKG_BEGIN_TOC) == 0;
}
bool BookMetadataCache::endTocPass() {
  const auto io = sink();
  return xp_pkg_cache_control(build, &io, XP_PKG_END_TOC) == 0;
}
bool BookMetadataCache::endWrite() {
  const auto io = sink();
  return xp_pkg_cache_control(build, &io, XP_PKG_END_WRITE) == 0;
}
void BookMetadataCache::createSpineEntry(const std::string& href) {
  const auto io = sink();
  if (xp_pkg_cache_entry(build, &io, 0, {}, {reinterpret_cast<const uint8_t*>(href.data()), href.size()}, {}, 0) != 0)
    LOG_ERR("BMC", "Could not write spine entry");
}
bool BookMetadataCache::createTocEntry(std::string_view title, std::string_view href, std::string_view anchor,
                                       uint8_t level) {
  const auto span = [](std::string_view value) {
    return XpPkgSpan{reinterpret_cast<const uint8_t*>(value.data()), value.size()};
  };
  const auto io = sink();
  return xp_pkg_cache_entry(build, &io, 1, span(title), span(href), span(anchor), level) == 0;
}
bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  spineCount = xp_pkg_cache_count(build, 0);
  tocCount = xp_pkg_cache_count(build, 1);
  BuildStreams buffers(*this, epubPath);
  const auto span = [](const std::string& v) {
    return XpPkgSpan{reinterpret_cast<const uint8_t*>(v.data()), v.size()};
  };
  const XpPkgSpan fields[] = {span(metadata.title), span(metadata.author), span(metadata.language),
                              span(metadata.coverItemHref), span(metadata.textReferenceHref)};
  const auto io = buffers.sink();
  const bool ok = xp_pkg_cache_build(build, &io, fields) == 0;
  if (!ok) LOG_ERR("BMC", "Failed building book.bin");
  return ok;
}
bool BookMetadataCache::cleanupTmpFiles() const {
  for (const char* suffix : {tmpSpineBinFile, tmpTocBinFile}) {
    const auto path = cachePath + suffix;
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }
  return true;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  xp_pkg_cache_destroy(build);
  build = nullptr;
  if (!Storage.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(bookFile, version);
  if (version != BOOK_CACHE_VERSION) {
    LOG_DBG("BMC", "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, version);
    // Explicit close() required: member variable persists beyond function scope
    bookFile.close();
    return false;
  }

  serialization::readPod(bookFile, lutOffset);
  serialization::readPod(bookFile, spineCount);
  serialization::readPod(bookFile, tocCount);

  serialization::readString(bookFile, coreMetadata.title);
  serialization::readString(bookFile, coreMetadata.author);
  serialization::readString(bookFile, coreMetadata.language);
  serialization::readString(bookFile, coreMetadata.coverItemHref);
  serialization::readString(bookFile, coreMetadata.textReferenceHref);

  // Spine entries follow the LUTs in index order; keep every SIZE_WINDOW-th cumulative size.
  windowStarts.clear();
  bookSize = 0;
  windowFirst = -1;
  windowStarts.reserve((spineCount + SIZE_WINDOW - 1) / SIZE_WINDOW);
  const uint32_t lutSize = (static_cast<uint32_t>(spineCount) + tocCount) * sizeof(uint32_t);
  bookFile.seek(lutOffset + lutSize);
  for (uint16_t i = 0; i < spineCount; i++) {
    if (i % SIZE_WINDOW == 0) windowStarts.push_back(bookSize);
    bookSize = readSpineEntry(bookFile).cumulativeSize;
  }

  loaded = true;
  LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

uint32_t BookMetadataCache::getCumulativeSize(const int index) const {
  if (!loaded || index < 0 || index >= spineCount) {
    return 0;
  }
  // Window edges come from RAM, so a page at a window's first item does not read the one before.
  if (index == spineCount - 1) return bookSize;
  const int first = index / SIZE_WINDOW * SIZE_WINDOW;
  if (index - first == SIZE_WINDOW - 1) return windowStarts[index / SIZE_WINDOW + 1];
  std::lock_guard<std::mutex> lock(sizeWindowLock);
  // A failed read answers the total before the window.
  if (windowFirst != first && !fillWindow(first)) return windowStarts[index / SIZE_WINDOW];
  return window[index - first];
}

bool BookMetadataCache::fillWindow(const int first) const {
  windowFirst = -1;
  // Own handle: getSpineEntry() moves bookFile.
  HalFile file;
  uint32_t pos = 0;
  const auto u32 = [&file](uint32_t& v) { return file.read(&v, sizeof(v)) == static_cast<int>(sizeof(v)); };
  bool ok = Storage.openFileForRead("BMC", cachePath + bookBinFile, file) &&
            file.seek(lutOffset + sizeof(uint32_t) * first) && u32(pos) && file.seek(pos);
  // Spine entry: href length, href, cumulative size, TOC index.
  for (int i = 0; ok && i < SIZE_WINDOW && first + i < spineCount; i++) {
    uint32_t hrefLen = 0;
    ok = u32(hrefLen) && file.seek(file.position() + hrefLen) && u32(window[i]) &&
         file.seek(file.position() + sizeof(int16_t));
  }
  if (ok) windowFirst = first;
  return ok;
}

int BookMetadataCache::getSpineIndexForSize(const uint32_t size) const {
  if (!loaded || spineCount == 0 || size > bookSize) return -1;
  // windowStarts[w] is the total through item w * SIZE_WINDOW - 1, so the answer lies in the window
  // before the first start that reaches `size`. Its last total comes from RAM and always reaches it.
  const auto next = std::lower_bound(windowStarts.begin() + 1, windowStarts.end(), size);
  const int first = static_cast<int>(next - windowStarts.begin() - 1) * SIZE_WINDOW;
  const int last = std::min<int>(spineCount, first + SIZE_WINDOW) - 1;
  std::lock_guard<std::mutex> lock(sizeWindowLock);
  // One refill attempt; on failure every total in the window falls back below `size`, as before.
  if (windowFirst != first && !fillWindow(first)) return last;
  for (int i = first; i < last; i++) {
    if (window[i - first] >= size) return i;
  }
  return last;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR("BMC", "getSpineEntry index %d out of range", index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * index);
  uint32_t spineEntryPos;
  serialization::readPod(bookFile, spineEntryPos);
  bookFile.seek(spineEntryPos);
  return readSpineEntry(bookFile);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR("BMC", "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR("BMC", "getTocEntry index %d out of range", index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index);
  uint32_t tocEntryPos;
  serialization::readPod(bookFile, tocEntryPos);
  bookFile.seek(tocEntryPos);
  return readTocEntry(bookFile);
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(HalFile& file) const {
  return readSpineEntryFrom(file);
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(HalFile& file) const { return readTocEntryFrom(file); }
