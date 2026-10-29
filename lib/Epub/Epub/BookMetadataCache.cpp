#include "BookMetadataCache.h"

#include <BufferedFile.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>
#include <ZipFile.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <optional>

#include "FsHelpers.h"
#include "parsers/PackageDiagnostics.h"

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

struct BookMetadataCache::ReadContext {
  const BookMetadataCache& owner;
  std::optional<std::lock_guard<std::mutex>> lock;
  std::optional<HalFile> input;
  uint32_t position = 0, hrefLength = 0;
  std::string temporary;
  explicit ReadContext(const BookMetadataCache& owner) : owner(owner) {}
  static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
    auto& c = *static_cast<ReadContext*>(raw);
    const auto& self = c.owner;
    switch (static_cast<XpBookCacheOperation>(operation)) {
      case XP_BOOK_CACHE_RESET_WRITER: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        xp_pkg_cache_destroy(mutableSelf.build);
        mutableSelf.build = nullptr;
        break;
      }
      case XP_BOOK_CACHE_OPEN: {
        return Storage.openFileForRead("BMC", self.cachePath + bookBinFile,
                                       const_cast<BookMetadataCache&>(self).bookFile);
      }
      case XP_BOOK_CACHE_READ_VERSION: {
        uint8_t version;
        serialization::readPod(const_cast<BookMetadataCache&>(self).bookFile, version);
        return version;
      }
      case XP_BOOK_CACHE_CLOSE: {
        const_cast<BookMetadataCache&>(self).bookFile.close();
        break;
      }
      case XP_BOOK_CACHE_READ_LUT: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        serialization::readPod(mutableSelf.bookFile, mutableSelf.lutOffset);
        break;
      }
      case XP_BOOK_CACHE_READ_SPINE_COUNT: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        serialization::readPod(mutableSelf.bookFile, mutableSelf.spineCount);
        break;
      }
      case XP_BOOK_CACHE_READ_TOC_COUNT: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        serialization::readPod(mutableSelf.bookFile, mutableSelf.tocCount);
        break;
      }
      case XP_BOOK_CACHE_READ_METADATA: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        auto& m = mutableSelf.coreMetadata;
        std::string* fields[] = {&m.title, &m.author, &m.language, &m.coverItemHref, &m.textReferenceHref};
        serialization::readString(mutableSelf.bookFile, *fields[argument]);
        break;
      }
      case XP_BOOK_CACHE_RESET_WINDOW: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        mutableSelf.windowStarts.clear();
        mutableSelf.bookSize = 0;
        mutableSelf.windowFirst = -1;
        break;
      }
      case XP_BOOK_CACHE_RESERVE_WINDOW: {
        const_cast<BookMetadataCache&>(self).windowStarts.reserve(argument);
        break;
      }
      case XP_BOOK_CACHE_SEEK_ENTRIES: {
        const_cast<BookMetadataCache&>(self).bookFile.seek(self.lutOffset + argument);
        break;
      }
      case XP_BOOK_CACHE_READ_CUMULATIVE: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        mutableSelf.bookSize = self.readSpineEntry(mutableSelf.bookFile).cumulativeSize;
        break;
      }
      case XP_BOOK_CACHE_ADD_WINDOW_START: {
        const_cast<BookMetadataCache&>(self).windowStarts.push_back(self.bookSize);
        break;
      }
      case XP_BOOK_CACHE_SET_LOADED: {
        const_cast<BookMetadataCache&>(self).loaded = argument;
        break;
      }
      case XP_BOOK_CACHE_SPINE_COUNT: {
        return self.spineCount;
      }
      case XP_BOOK_CACHE_TOC_COUNT: {
        return self.tocCount;
      }
      case XP_BOOK_CACHE_BOOK_SIZE: {
        return self.bookSize;
      }
      case XP_BOOK_CACHE_LOADED: {
        return self.loaded;
      }
      case XP_BOOK_CACHE_WINDOW_FIRST: {
        return self.windowFirst;
      }
      case XP_BOOK_CACHE_SET_WINDOW_FIRST: {
        self.windowFirst = static_cast<int32_t>(argument);
        break;
      }
      case XP_BOOK_CACHE_WINDOW_START: {
        return self.windowStarts[argument];
      }
      case XP_BOOK_CACHE_WINDOW_VALUE: {
        return self.window[argument];
      }
      case XP_BOOK_CACHE_WINDOW_COUNT: {
        return self.windowStarts.size();
      }
      case XP_BOOK_CACHE_LOCK: {
        c.lock.emplace(sizeWindowLock);
        break;
      }
      case XP_BOOK_CACHE_FILL_OPEN: {
        c.input.emplace();
        return Storage.openFileForRead("BMC", self.cachePath + bookBinFile, *c.input);
      }
      case XP_BOOK_CACHE_FILL_SEEK_LUT: {
        return c.input->seek(self.lutOffset + sizeof(uint32_t) * argument);
      }
      case XP_BOOK_CACHE_FILL_READ_POSITION: {
        return c.input->read(&c.position, sizeof(c.position)) == sizeof(c.position);
      }
      case XP_BOOK_CACHE_FILL_SEEK_POSITION: {
        return c.input->seek(c.position);
      }
      case XP_BOOK_CACHE_FILL_READ_HREF: {
        c.hrefLength = 0;
        return c.input->read(&c.hrefLength, sizeof(c.hrefLength)) == sizeof(c.hrefLength);
      }
      case XP_BOOK_CACHE_FILL_SKIP_HREF: {
        return c.input->seek(c.input->position() + c.hrefLength);
      }
      case XP_BOOK_CACHE_FILL_READ_WINDOW: {
        return c.input->read(&self.window[argument], sizeof(uint32_t)) == sizeof(uint32_t);
      }
      case XP_BOOK_CACHE_FILL_SKIP_TOC: {
        return c.input->seek(c.input->position() + sizeof(int16_t));
      }
      case XP_BOOK_CACHE_PREPARE_TEMPORARY:
        c.temporary = self.cachePath + (argument == 0 ? tmpSpineBinFile : tmpTocBinFile);
        break;
      case XP_BOOK_CACHE_TEMPORARY_EXISTS:
        return Storage.exists(c.temporary.c_str());
      case XP_BOOK_CACHE_TEMPORARY_REMOVE:
        return Storage.remove(c.temporary.c_str());
      case XP_BOOK_CACHE_DROP_TEMPORARY:
        std::string{}.swap(c.temporary);
        break;
      case XP_BOOK_CACHE_ENTRY_SEEK:
        const_cast<BookMetadataCache&>(self).bookFile.seek(self.lutOffset + argument);
        break;
      case XP_BOOK_CACHE_ENTRY_POSITION: {
        auto& file = const_cast<BookMetadataCache&>(self).bookFile;
        uint32_t position;
        serialization::readPod(file, position);
        file.seek(position);
        break;
      }
      case XP_BOOK_CACHE_FILL_DROP: {
        c.input.reset();
        break;
      }
    }
    return 0;
  }
  static void diagnostic(void* raw, uint32_t event, uint32_t argument) {
    const auto& self = static_cast<ReadContext*>(raw)->owner;
    switch (event) {
      case 0:
        LOG_DBG("BMC", "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, argument);
        break;
      case 1:
        LOG_DBG("BMC", "Loaded cache data: %d spine, %d TOC entries", self.spineCount, self.tocCount);
        break;
      case 2:
        LOG_ERR("BMC", "getSpineEntry called but cache not loaded");
        break;
      case 3:
        LOG_ERR("BMC", "getSpineEntry index %d out of range", static_cast<int32_t>(argument));
        break;
      case 4:
        LOG_ERR("BMC", "getTocEntry called but cache not loaded");
        break;
      case 5:
        LOG_ERR("BMC", "getTocEntry index %d out of range", static_cast<int32_t>(argument));
        break;
    }
  }
  XpBookServices services() { return {this, operation, nullptr, diagnostic}; }
};

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
  bool flushFailed = false;
  bool sizeOpenFailed = false;
  explicit BuildStreams(BookMetadataCache& owner, const std::string& path)
      : owner(owner), path(path), isZip(!FsHelpers::hasTxtExtension(path) && !FsHelpers::hasMarkdownExtension(path)) {}
  struct SizeContext {
    BuildStreams& streams;
    std::optional<std::deque<ZipFile::SizeTarget>> targets;
    explicit SizeContext(BuildStreams& streams) : streams(streams) {}
    static uint32_t operation(void* raw, uint32_t operation, uint32_t argument) {
      auto& c = *static_cast<SizeContext*>(raw);
      auto& self = c.streams;
      switch (static_cast<XpBookSizeOperation>(operation)) {
        case XP_BOOK_SIZE_ZIP:
          return self.isZip;
        case XP_BOOK_SIZE_RAW_SIZE: {
          HalFile raw;
          if (Storage.openFileForRead("BMC", self.path, raw)) self.rawSize = raw.size();
          break;
        }
        case XP_BOOK_SIZE_OPEN_ZIP:
          self.zip.emplace(self.path);
          self.sizeOpenFailed = !self.zip->open();
          return !self.sizeOpenFailed;
        case XP_BOOK_SIZE_COUNT:
          return self.owner.spineCount;
        case XP_BOOK_SIZE_ALLOCATE_SIZES:
          c.targets.emplace();
          self.sizes.emplace(argument, 0);
          break;
        case XP_BOOK_SIZE_REWIND:
          self.spine->seek(0);
          break;
        case XP_BOOK_SIZE_APPEND: {
          const auto entry = readSpineEntryFrom(*self.spine);
          const auto name = FsHelpers::normalisePath(entry.href);
          c.targets->push_back({ZipFile::fnvHash64(name.c_str(), name.size()), static_cast<uint16_t>(name.size()),
                                static_cast<uint16_t>(argument)});
          break;
        }
        case XP_BOOK_SIZE_TARGETS:
          return c.targets->size();
        case XP_BOOK_SIZE_SORT:
          std::sort(c.targets->begin(), c.targets->end(),
                    [](const auto& a, const auto& b) { return xp_book_size_less(a.hash, a.len, b.hash, b.len); });
          break;
        case XP_BOOK_SIZE_FILL:
          return self.zip->fillUncompressedSizes(*c.targets, *self.sizes);
        case XP_BOOK_SIZE_CLEAR:
          c.targets->clear();
          break;
      }
      return 0;
    }
    static void diagnostic(void* raw, uint32_t event, uint32_t argument) {
      auto& self = static_cast<SizeContext*>(raw)->streams;
      switch (static_cast<XpBookSizeEvent>(event)) {
        case XP_BOOK_SIZE_EVENT_OPEN_FAILED:
          LOG_ERR("BMC", "Could not open EPUB zip for size calculations");
          break;
        case XP_BOOK_SIZE_EVENT_BATCH:
          LOG_DBG("BMC", "Using batch size lookup for %d spine items", self.owner.spineCount);
          break;
        case XP_BOOK_SIZE_EVENT_MATCHED:
          LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", argument, self.owner.spineCount);
          break;
      }
    }
  };
  bool beginSizes() {
    SizeContext context{*this};
    const XpBookServices io{&context, SizeContext::operation, nullptr, SizeContext::diagnostic};
    return xp_book_sizes(&io);
  }
  static void diagnostic(void* context, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                         const uint8_t* text, uint32_t size, const uint8_t* other, uint32_t otherSize) {
    if (event == XP_PKG_DIAG_MISSING_SPINE_TITLE && !static_cast<BuildStreams*>(context)->isZip) return;
    packageDiagnostic(nullptr, event, a, b, c, d, text, size, other, otherSize);
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
        self.flushFailed = !self.book->flush();
        return !self.flushFailed;
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
    struct Context {
      BuildStreams& state;
      XpPkgSpan href;
    } c{state, href};
    const XpBookItemSizeServices services{
        &c, [](void* raw, uint32_t operation, size_t index) -> size_t {
          auto& c = *static_cast<Context*>(raw);
          auto& state = c.state;
          switch (operation) {
            case 0:
              return state.rawSize;
            case 1:
              return state.sizes ? (*state.sizes)[index] : 0;
            case 2: {
              size_t size = 0;
              const auto path =
                  FsHelpers::normalisePath(std::string_view(reinterpret_cast<const char*>(c.href.data), c.href.len));
              if (!state.zip->getInflatedFileSize(path.c_str(), &size))
                LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", path.c_str());
              return size;
            }
          }
          return 0;
        }};
    return xp_book_cache_item_size(&services, index, state.isZip);
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
  LOG_DBG("BMC", "Entering write mode");
  return true;
}
bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG("BMC", "Beginning content opf pass");
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  const bool ok = xp_pkg_cache_control(build, &io, XP_PKG_BEGIN_OPF, &diagnostics) == 0;
  return ok;
}
bool BookMetadataCache::endContentOpfPass() {
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  const bool ok = xp_pkg_cache_control(build, &io, XP_PKG_END_OPF, &diagnostics) == 0;
  if (!ok) LOG_ERR("BMC", "Failed writing spine tmp file");
  return ok;
}
bool BookMetadataCache::beginTocPass() {
  LOG_DBG("BMC", "Beginning toc pass");
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  const bool ok = xp_pkg_cache_control(build, &io, XP_PKG_BEGIN_TOC, &diagnostics) == 0;
  return ok;
}
bool BookMetadataCache::endTocPass() {
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  const bool ok = xp_pkg_cache_control(build, &io, XP_PKG_END_TOC, &diagnostics) == 0;
  if (!ok) LOG_ERR("BMC", "Failed writing toc tmp file");
  return ok;
}
bool BookMetadataCache::endWrite() {
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  const bool ok = xp_pkg_cache_control(build, &io, XP_PKG_END_WRITE, &diagnostics) == 0;
  if (!ok)
    LOG_DBG("BMC", "endWrite called but not in build mode");
  else
    LOG_DBG("BMC", "Wrote %d spine, %d TOC entries", xp_pkg_cache_count(build, 0), xp_pkg_cache_count(build, 1));
  return ok;
}
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!build) {
    LOG_DBG("BMC", "createSpineEntry called but not in build mode");
    return;
  }
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  if (xp_pkg_cache_entry(build, &io, 0, {}, {reinterpret_cast<const uint8_t*>(href.data()), href.size()}, {}, 0,
                         &diagnostics) != 0)
    LOG_ERR("BMC", "Could not write spine entry");
}
bool BookMetadataCache::createTocEntry(std::string_view title, std::string_view href, std::string_view anchor,
                                       uint8_t level) {
  if (!build) {
    LOG_DBG("BMC", "createTocEntry called but not in build mode");
    return false;
  }
  const auto span = [](std::string_view value) {
    return XpPkgSpan{reinterpret_cast<const uint8_t*>(value.data()), value.size()};
  };
  const auto io = sink();
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  return xp_pkg_cache_entry(build, &io, 1, span(title), span(href), span(anchor), level, &diagnostics) == 0;
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
  const XpPkgDiagnostics diagnostics{&buffers, BuildStreams::diagnostic};
  const bool ok = xp_pkg_cache_build(build, &io, fields, &diagnostics) == 0;
  if (buffers.flushFailed)
    LOG_ERR("BMC", "Failed writing book.bin, removing truncated file");
  else if (!ok && !buffers.sizeOpenFailed)
    LOG_ERR("BMC", "Failed building book.bin");
  if (ok) LOG_DBG("BMC", "Successfully built book.bin");
  return ok;
}
bool BookMetadataCache::cleanupTmpFiles() const {
  ReadContext context{*this};
  const auto io = context.services();
  return xp_book_metadata_cleanup(&io);
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  ReadContext context{*this};
  const auto io = context.services();
  return xp_book_metadata_load(&io);
}

uint32_t BookMetadataCache::getCumulativeSize(const int index) const {
  ReadContext context{*this};
  const auto io = context.services();
  return xp_book_metadata_cumulative(&io, index);
}

int BookMetadataCache::getSpineIndexForSize(const uint32_t size) const {
  ReadContext context{*this};
  const auto io = context.services();
  return xp_book_metadata_spine(&io, size);
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  ReadContext context{*this};
  const auto io = context.services();
  if (!xp_book_metadata_entry(&io, index, false)) return {};
  return readSpineEntry(bookFile);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  ReadContext context{*this};
  const auto io = context.services();
  if (!xp_book_metadata_entry(&io, index, true)) return {};
  return readTocEntry(bookFile);
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(HalFile& file) const {
  return readSpineEntryFrom(file);
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(HalFile& file) const { return readTocEntryFrom(file); }
