#include <BufferedFile.h>
#include <Logging.h>
#include <ZipFile.h>
#include <xp_book.h>
#include <xp_ffi.h>

#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <new>
#include <optional>

#include "BookMetadataCache.h"
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

}  // namespace

struct BookMetadataCache::ReadContext {
  const BookMetadataCache& owner;
  std::optional<std::lock_guard<std::mutex>> lock;
  std::optional<HalFile> input;
  std::optional<SpineEntry> spine;
  TocEntry* toc = nullptr;
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

      case XP_BOOK_CACHE_CLOSE: {
        const_cast<BookMetadataCache&>(self).bookFile.close();
        break;
      }

      case XP_BOOK_CACHE_RESET_WINDOW: {
        auto& mutableSelf = const_cast<BookMetadataCache&>(self);
        mutableSelf.windowStarts.clear();
        break;
      }
      case XP_BOOK_CACHE_RESERVE_WINDOW: {
        const_cast<BookMetadataCache&>(self).windowStarts.reserve(argument);
        break;
      }
      case XP_BOOK_CACHE_SEEK: {
        const_cast<BookMetadataCache&>(self).bookFile.seek(argument);
        break;
      }

      case XP_BOOK_CACHE_ADD_WINDOW_START: {
        const_cast<BookMetadataCache&>(self).windowStarts.push_back(argument);
        break;
      }

      case XP_BOOK_CACHE_WINDOW_START: {
        return self.windowStarts[argument];
      }

      case XP_BOOK_CACHE_WINDOW_COUNT: {
        return self.windowStarts.size();
      }
      case XP_BOOK_CACHE_LOCK: {
        c.lock.emplace(sizeWindowLock);
        break;
      }
      case XP_BOOK_CACHE_FILL_FILE: {
        if (!argument) {
          c.input.reset();
          return 1;
        }
        c.input.emplace();
        return Storage.openFileForRead("BMC", self.cachePath + bookBinFile, *c.input);
      }
      case XP_BOOK_CACHE_FILL_SEEK: {
        return c.input->seek(argument);
      }

      case XP_BOOK_CACHE_FILL_POSITION: {
        return c.input->position();
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

      case XP_BOOK_CACHE_SPINE_RECORD:
        if (argument)
          c.spine.emplace();
        else
          c.spine.reset();
        break;
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
  static size_t readBytes(void* raw, uint32_t file, uint8_t* bytes, size_t size) {
    auto& c = *static_cast<ReadContext*>(raw);
    return file == 0 ? const_cast<BookMetadataCache&>(c.owner).bookFile.read(bytes, size) : c.input->read(bytes, size);
  }
  static uint8_t* field(void* raw, uint32_t id, size_t size) {
    auto& c = *static_cast<ReadContext*>(raw);
    auto& self = const_cast<BookMetadataCache&>(c.owner);
    static_assert(sizeof(bool) == 1 && sizeof(int) == 4);
    std::string* text = nullptr;
    switch (id) {
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_LUT):
        return reinterpret_cast<uint8_t*>(&self.lutOffset);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_SPINE_COUNT):
        return reinterpret_cast<uint8_t*>(&self.spineCount);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TOC_COUNT):
        return reinterpret_cast<uint8_t*>(&self.tocCount);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TITLE):
        text = &self.coreMetadata.title;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_AUTHOR):
        text = &self.coreMetadata.author;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_LANGUAGE):
        text = &self.coreMetadata.language;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_COVER):
        text = &self.coreMetadata.coverItemHref;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TEXT):
        text = &self.coreMetadata.textReferenceHref;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_HREF):
        text = &c.spine->href;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_CUMULATIVE):
        return reinterpret_cast<uint8_t*>(&c.spine->cumulativeSize);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TOC_INDEX):
        return reinterpret_cast<uint8_t*>(&c.spine->tocIndex);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TOC_TITLE):
        text = &c.toc->title;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TOC_HREF):
        text = &c.toc->href;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_TOC_ANCHOR):
        text = &c.toc->anchor;
        break;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_LEVEL):
        return &c.toc->level;
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_SPINE_INDEX):
        return reinterpret_cast<uint8_t*>(&c.toc->spineIndex);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_BOOK_SIZE):
        return reinterpret_cast<uint8_t*>(&self.bookSize);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_LOADED):
        return reinterpret_cast<uint8_t*>(&self.loaded);
      case static_cast<uint32_t>(XpBookCacheField::XP_BOOK_FIELD_WINDOW_FIRST):
        return reinterpret_cast<uint8_t*>(&self.windowFirst);
      default:
        return reinterpret_cast<uint8_t*>(&self.window[id - XP_BOOK_FIELD_WINDOW]);
    }
    text->resize(size);
    return reinterpret_cast<uint8_t*>(text->data());
  }
  XpBookCacheServices services() { return {this, operation, readBytes, field, diagnostic}; }
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
  bool flushFailed = false;
  bool sizeOpenFailed = false;
  explicit BuildStreams(BookMetadataCache& owner, const std::string& path) : owner(owner), path(path) {}
  std::optional<std::deque<ZipFile::SizeTarget>> targets;
  std::optional<SpineEntry> entry;
  std::optional<std::string> name;
  static uint64_t sizeStorage(void* raw, uint32_t operation, uint32_t index, void* value) {
    auto& self = *static_cast<BuildStreams*>(raw);
    auto* span = static_cast<XpPkgSpan*>(value);
    // Targets cross as hash/length/index values, without aliasing the native struct.
    auto* fields = static_cast<uint64_t*>(value);
    switch (static_cast<XpBookSizeOperation>(operation)) {
      case XP_BOOK_SIZE_PATH:
        *span = {reinterpret_cast<const uint8_t*>(self.path.data()), self.path.size()};
        break;
      case XP_BOOK_SIZE_RAW_SIZE: {
        HalFile raw;
        if (Storage.openFileForRead("BMC", self.path, raw)) self.rawSize = raw.size();
        return self.rawSize;
      }
      case XP_BOOK_SIZE_OPEN_ZIP:
        self.zip.emplace(self.path);
        self.sizeOpenFailed = !self.zip->open();
        return !self.sizeOpenFailed;
      case XP_BOOK_SIZE_ALLOCATE_TARGETS:
        self.targets.emplace();
        break;
      case XP_BOOK_SIZE_ALLOCATE_SIZES:
        self.sizes.emplace(index, 0);
        break;
      case XP_BOOK_SIZE_BEGIN_ENTRY:
        self.entry.emplace();
        break;
      case XP_BOOK_SIZE_HREF:
        self.entry->href.resize(index);
        *span = {reinterpret_cast<const uint8_t*>(self.entry->href.data()), self.entry->href.size()};
        break;
      case XP_BOOK_SIZE_NORMALIZE:
        self.name.emplace(FsHelpers::normalisePath(self.entry->href));
        *span = {reinterpret_cast<const uint8_t*>(self.name->data()), self.name->size()};
        break;
      case XP_BOOK_SIZE_APPEND:
        self.targets->push_back({fields[0], static_cast<uint16_t>(fields[1]), static_cast<uint16_t>(fields[2])});
        break;
      case XP_BOOK_SIZE_END_ENTRY:
        self.name.reset();
        self.entry.reset();
        break;
      case XP_BOOK_SIZE_TARGET:
        fields[0] = (*self.targets)[index].hash;
        fields[1] = (*self.targets)[index].len;
        fields[2] = (*self.targets)[index].index;
        break;
      case XP_BOOK_SIZE_SET_TARGET:
        (*self.targets)[index] = {fields[0], static_cast<uint16_t>(fields[1]), static_cast<uint16_t>(fields[2])};
        break;
      case XP_BOOK_SIZE_FILL:
        return self.zip->fillUncompressedSizes(*self.targets, *self.sizes);
      case XP_BOOK_SIZE_CLEAR:
        self.targets->clear();
        break;
      case XP_BOOK_SIZE_DROP_TARGETS:
        self.targets.reset();
        break;
      case XP_BOOK_SIZE_CACHED:
        return self.sizes ? (*self.sizes)[index] : 0;
      case XP_BOOK_SIZE_LOOKUP: {
        size_t size = 0;
        self.name.emplace(
            FsHelpers::normalisePath(std::string_view(reinterpret_cast<const char*>(span->data), span->len)));
        const bool found = self.zip->getInflatedFileSize(self.name->c_str(), &size);
        // The high word reports failure; the low word is the ZIP size.
        return static_cast<uint32_t>(size) | (static_cast<uint64_t>(!found) << 32);
      }
      case XP_BOOK_SIZE_ERROR:
        if (index == 0)
          LOG_ERR("BMC", "Could not open EPUB zip for size calculations");
        else
          LOG_ERR("BMC", "Warning: Could not get size for spine item: %s", self.name->c_str());
        break;
      case XP_BOOK_SIZE_BATCH:
        LOG_DBG("BMC", "Using batch size lookup for %d spine items", index);
        break;
      case XP_BOOK_SIZE_MATCHED:
        LOG_DBG("BMC", "Batch lookup matched %d/%d spine items", index, self.owner.spineCount);
        break;
    }
    return 0;
  }
  static void diagnostic(void*, uint32_t event, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const uint8_t* text,
                         uint32_t size, const uint8_t* other, uint32_t otherSize) {
    packageDiagnostic(nullptr, event, a, b, c, d, text, size, other, otherSize);
  }
  XpPkgIo sink() { return {this, control, read, write, seek, sizeStorage, nullptr}; }
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
  return std::move(*context.spine);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  ReadContext context{*this};
  TocEntry entry;
  context.toc = &entry;
  const auto io = context.services();
  if (!xp_book_metadata_entry(&io, index, true)) return {};
  return entry;
}

#include <Logging.h>
#include <expat.h>

#include "Epub/BookMetadataCache.h"
#include "parsers/PackageDiagnostics.h"
#include "parsers/PackageParser.h"

PackageParser::~PackageParser() { xp_pkg_pass_destroy(pass); }
bool PackageParser::setup() {
  xp_pkg_pass_destroy(pass);
  pass = nullptr;
  const XpPkgIo itemIo{this, control, read, write, seek, nullptr, nullptr};
  const auto cacheIo = cache ? cache->sink() : XpPkgIo{};
  if (xp_pkg_pass_create(kind, {reinterpret_cast<const uint8_t*>(base.data()), base.size()}, size, metadataOnly,
                         cache ? &itemIo : nullptr, cache ? cache->writer() : nullptr, cache ? &cacheIo : nullptr,
                         &pass) != 0) {
    switch (kind) {
      case XP_PKG_CONTAINER:
        LOG_ERR("CTR", "Couldn't allocate memory for parser");
        break;
      case XP_PKG_OPF_PASS:
        LOG_DBG("COF", "Couldn't allocate memory for parser");
        break;
      case XP_PKG_NAV_PASS:
        LOG_DBG("NAV", "Couldn't allocate memory for parser");
        break;
      case XP_PKG_NCX_PASS:
        LOG_DBG("TOC", "Couldn't allocate memory for parser");
        break;
      default:
        LOG_ERR("PKG", "Could not allocate package pass");
        break;
    }
    return false;
  }
  return true;
}
void PackageParser::logError(const XpPkgError& error) const {
  if (!error.code) return;
  if (error.buffer) {
    switch (kind) {
      case XP_PKG_CONTAINER:
        LOG_DBG("CTR", "Couldn't allocate buffer");
        break;
      case XP_PKG_OPF_PASS:
        LOG_ERR("COF", "Couldn't allocate memory for buffer");
        break;
      case XP_PKG_NAV_PASS:
        LOG_DBG("NAV", "Couldn't allocate memory for buffer");
        break;
      case XP_PKG_NCX_PASS:
        LOG_DBG("TOC", "Couldn't allocate memory for buffer");
        break;
      default:
        break;
    }
    return;
  }
  const char* message = XML_ErrorString(static_cast<XML_Error>(error.code));
  const auto line = static_cast<unsigned long>(error.line);
  switch (kind) {
    case XP_PKG_CONTAINER:
      LOG_ERR("CTR", "Parse error: %s", message);
      break;
    case XP_PKG_OPF_PASS:
      LOG_DBG("COF", "Parse error at line %lu: %s", line, message);
      break;
    case XP_PKG_NAV_PASS:
      LOG_DBG("NAV", "Parse error at line %lu: %s", line, message);
      break;
    case XP_PKG_NCX_PASS:
      LOG_DBG("TOC", "Parse error at line %lu: %s", line, message);
      break;
    default:
      break;
  }
}
size_t PackageParser::write(const uint8_t* bytes, size_t len) {
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  XpPkgError error{};
  const auto count = xp_pkg_pass_write(pass, {bytes, len}, &diagnostics, &error);
  logError(error);
  return count;
}
bool PackageParser::finish() {
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  XpPkgError error{};
  const bool ok = xp_pkg_pass_finish(pass, &diagnostics, &error);
  logError(error);
  return ok;
}
std::string_view PackageParser::view(uint32_t field) const {
  const auto value = xp_pkg_pass_value(pass, field);
  return {reinterpret_cast<const char*>(value.data), value.len};
}
std::string PackageParser::value(uint32_t field) const { return std::string(view(field)); }
std::optional<float> PackageParser::seriesIndex() const {
  float value;
  if (xp_pkg_pass_index(pass, &value)) return value;
  return std::nullopt;
}
std::vector<std::string> PackageParser::cssFiles() const {
  std::vector<std::string> result;
  const size_t count = xp_pkg_pass_count(pass);
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) result.push_back(value(32 + i));
  return result;
}
uint32_t PackageParser::control(void* context, uint32_t, uint32_t operation) {
  auto& self = *static_cast<PackageParser*>(context);
  auto& file = self.items;
  switch (operation) {
    case XP_PKG_OPEN_READ: {
      const bool ok = Storage.openFileForRead("COF", std::string(self.cachePath) + "/.items.bin", file);
      if (!ok) LOG_ERR("COF", "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
      return ok;
    }
    case XP_PKG_OPEN_WRITE: {
      const bool ok = Storage.openFileForWrite("COF", std::string(self.cachePath) + "/.items.bin", file);
      if (!ok) LOG_ERR("COF", "Couldn't open temp items file for writing. This is probably going to be a fatal error.");
      return ok;
    }
    case XP_PKG_CLOSE:
      file.close();
      return 1;
    case XP_PKG_REMOVE: {
      const auto path = std::string(self.cachePath) + "/.items.bin";
      return !Storage.exists(path.c_str()) || Storage.remove(path.c_str());
    }
    case XP_PKG_IS_OPEN:
      return static_cast<bool>(file);
    case XP_PKG_POSITION:
      return file.position();
    case XP_PKG_SIZE:
      return file.size();
    default:
      return 0;
  }
}
size_t PackageParser::read(void* context, uint32_t, uint8_t* bytes, size_t size) {
  const int read = static_cast<PackageParser*>(context)->items.read(bytes, size);
  return read < 0 ? 0 : static_cast<size_t>(read);
}
size_t PackageParser::write(void* context, uint32_t, const uint8_t* bytes, size_t size) {
  return static_cast<PackageParser*>(context)->items.write(bytes, size);
}
uint8_t PackageParser::seek(void* context, uint32_t, uint32_t offset) {
  return static_cast<PackageParser*>(context)->items.seek(offset);
}
