#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <optional>

#include "ZipFile.h"

namespace {
static_assert(sizeof(XpZipEntry) == 32 && alignof(XpZipEntry) == 4);
static_assert(offsetof(XpZipEntry, compressed_size) == 16 && offsetof(XpZipEntry, method) == 28);
// RAII zip: opens the zip if not already open, closes on destruction only if
// it performed the open.  Removes the wasOpen/close boilerplate from every method.
class ScopedOpenClose final {
 public:
  [[nodiscard]] explicit ScopedOpenClose(ZipFile& zf) : zf(zf), needsClose(!zf.isOpen()) {
    if (needsClose) ok = zf.open();
  }
  ~ScopedOpenClose() {
    if (needsClose && ok) zf.close();
  }
  ScopedOpenClose(const ScopedOpenClose&) = delete;
  ScopedOpenClose& operator=(const ScopedOpenClose&) = delete;
  ScopedOpenClose(ScopedOpenClose&&) = delete;
  ScopedOpenClose& operator=(ScopedOpenClose&&) = delete;
  explicit operator bool() const { return ok || !needsClose; }

 private:
  ZipFile& zf;
  bool needsClose = false;
  bool ok = true;  // true when zip was already open (no open() call needed)
};

enum class ExtractEvent : uint32_t {
  ReadCompressed = 1,
  OutputAllocation,
  ReadData,
  InputAllocation,
  Init,
  Inflate,
  Method,
  BufferAllocation,
  ReadMore,
  Write,
  StreamOutputAllocation,
  Exceeds,
  Mismatch,
  Decompressed,
  Failed
};
struct ExtractContext {
  HalFile& file;
  Print* output;
  const char* filename;
  InflateStream inflate;
};
XpBookZipServices extractServices(ExtractContext& context) {
  return {&context,
          [](void* ctx, uint8_t* bytes, size_t size) -> int32_t {
            return static_cast<ExtractContext*>(ctx)->file.read(bytes, size);
          },
          [](void*, size_t size) { return static_cast<uint8_t*>(malloc(size)); },
          [](void*, uint8_t* bytes) { free(bytes); },
          [](void* ctx, bool streaming, XpBookZipFill fill, void* fillContext) {
            auto& inflate = static_cast<ExtractContext*>(ctx)->inflate;
            if (!inflate.init(streaming)) return false;
            inflate.setFill(fill, fillContext);
            return true;
          },
          [](void* ctx, uint8_t* bytes, size_t size) {
            return static_cast<ExtractContext*>(ctx)->inflate.read(bytes, size);
          },
          [](void* ctx, uint8_t* bytes, size_t size, size_t* produced) -> int32_t {
            return static_cast<int32_t>(static_cast<ExtractContext*>(ctx)->inflate.readAtMost(bytes, size, produced));
          },
          [](void* ctx, const uint8_t* bytes, size_t size) {
            return static_cast<ExtractContext*>(ctx)->output->write(bytes, size);
          },
          [](void* opaque, uint32_t event, uint32_t a, uint32_t b, uint32_t, uint32_t) {
            [[maybe_unused]] const auto& ctx = *static_cast<ExtractContext*>(opaque);
            switch (static_cast<ExtractEvent>(event)) {
              case ExtractEvent::ReadCompressed:
                LOG_ERR("ZIP", "Failed to read compressed data: %d", static_cast<int32_t>(a));
                break;
              case ExtractEvent::OutputAllocation:
                LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", static_cast<size_t>(a));
                break;
              case ExtractEvent::ReadData:
                LOG_ERR("ZIP", "Failed to read data");
                break;
              case ExtractEvent::InputAllocation:
                LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
                break;
              case ExtractEvent::Init:
                LOG_ERR("ZIP", "Failed to init inflate stream for %s", ctx.filename);
                break;
              case ExtractEvent::Inflate:
                LOG_ERR("ZIP", "Failed to inflate file");
                break;
              case ExtractEvent::Method:
                LOG_ERR("ZIP", "Unsupported compression method");
                break;
              case ExtractEvent::BufferAllocation:
                LOG_ERR("ZIP", "Failed to allocate memory for buffer");
                break;
              case ExtractEvent::ReadMore:
                LOG_ERR("ZIP", "Could not read more bytes");
                break;
              case ExtractEvent::Write:
                LOG_ERR("ZIP", "Failed to write all output bytes to stream");
                break;
              case ExtractEvent::StreamOutputAllocation:
                LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
                break;
              case ExtractEvent::Exceeds:
                LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", static_cast<size_t>(a),
                        static_cast<size_t>(b));
                break;
              case ExtractEvent::Mismatch:
                LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(a),
                        static_cast<size_t>(b));
                break;
              case ExtractEvent::Decompressed:
                LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", a, b);
                break;
              case ExtractEvent::Failed:
                LOG_ERR("ZIP", "Decompression failed");
                break;
            }
          }};
}
}  // namespace

struct ZipFile::IndexContext {
  ZipFile& zip;
  const char* target = nullptr;
  void* visitContext = nullptr;
  void (*visit)(void*, std::string_view, uint32_t, uint32_t) = nullptr;
  std::optional<ScopedOpenClose> opened;
  char name[256];
  IndexContext(ZipFile& zip, const char* target = nullptr, void* visitContext = nullptr,
               void (*visit)(void*, std::string_view, uint32_t, uint32_t) = nullptr)
      : zip(zip), target(target), visitContext(visitContext), visit(visit) {}
};
XpBookZipIndexServices ZipFile::indexServices(IndexContext& context) {
  return {&context,
          [](void* opaque, uint32_t operation, uint32_t argument) -> size_t {
            auto& ctx = *static_cast<IndexContext*>(opaque);
            auto& zip = ctx.zip;
            switch (static_cast<XpBookZipIndexOp>(operation)) {
              case XP_BOOK_ZIP_INDEX_CACHE_SIZE:
                return zip.fileStatSlimCache.size();
              case XP_BOOK_ZIP_INDEX_OPEN:
                if (!ctx.opened) ctx.opened.emplace(zip);
                return bool(*ctx.opened);
              case XP_BOOK_ZIP_INDEX_DETAILS:
                return zip.loadZipDetails();
              case XP_BOOK_ZIP_INDEX_CENTRAL:
                return zip.zipDetails.centralDirOffset;
              case XP_BOOK_ZIP_INDEX_CLEAR:
              case XP_BOOK_ZIP_INDEX_RESERVE:
                return 0;
              case XP_BOOK_ZIP_INDEX_CURSOR:
                zip.lastCentralDirPos = argument;
                zip.lastCentralDirPosValid = true;
                return 1;
              case XP_BOOK_ZIP_INDEX_LAST_VALID:
                return zip.lastLookupValid;
              case XP_BOOK_ZIP_INDEX_CURSOR_VALID:
                return zip.lastCentralDirPosValid;
              case XP_BOOK_ZIP_INDEX_CURSOR_VALUE:
                return zip.lastCentralDirPos;
            }
            return 0;
          },
          [](void* opaque, size_t offset) { return static_cast<IndexContext*>(opaque)->zip.file.seek(offset); },
          [](void* opaque, uint8_t* bytes, size_t size) -> int32_t {
            return static_cast<IndexContext*>(opaque)->zip.file.read(bytes, size);
          },
          [](void* opaque, uint32_t operation, XpZipEntry* entry, XpBookBorrow name) {
            auto& ctx = *static_cast<IndexContext*>(opaque);
            auto& zip = ctx.zip;
            const auto path = std::string_view(reinterpret_cast<const char*>(name.data), name.len);

            switch (static_cast<XpBookZipCacheOp>(operation)) {
              case XP_BOOK_ZIP_CACHE_FIND: {
                const auto found = zip.fileStatSlimCache.find(ctx.target);
                if (found == zip.fileStatSlimCache.end()) return false;
                *entry = {{},
                          0,
                          0,
                          found->second.compressedSize,
                          found->second.uncompressedSize,
                          found->second.localHeaderOffset,
                          found->second.method};
                return true;
              }
              case XP_BOOK_ZIP_CACHE_LAST:
                *entry = {{},
                          0,
                          0,
                          zip.lastLookup.compressedSize,
                          zip.lastLookup.uncompressedSize,
                          zip.lastLookup.localHeaderOffset,
                          zip.lastLookup.method};
                return true;
              case XP_BOOK_ZIP_CACHE_REMEMBER:
                zip.lastLookupName.assign(path);
                zip.lastLookup = {entry->method, entry->compressed_size, entry->uncompressed_size, entry->local_offset};
                zip.lastLookupValid = true;
                return true;
              case XP_BOOK_ZIP_CACHE_INSERT:
                return false;
              case XP_BOOK_ZIP_CACHE_PATHS:
                for (const auto& item : zip.fileStatSlimCache) ctx.visit(ctx.visitContext, item.first, 0, 0);
                return true;
              case XP_BOOK_ZIP_CACHE_VISIT:
                ctx.visit(ctx.visitContext, path, entry->crc32, entry->compressed_size);
                return true;
            }
            return false;
          },
          reinterpret_cast<uint8_t*>(context.name),
          [](void* opaque, uint32_t event, uint32_t expected, int32_t actual) {
            [[maybe_unused]] auto& ctx = *static_cast<IndexContext*>(opaque);
            switch (static_cast<XpBookZipDiagnostic>(event)) {
              case XP_BOOK_ZIP_LOG_CENTRAL_SEEK:
                LOG_ERR("ZIP", "Failed to seek central directory");
                break;
              case XP_BOOK_ZIP_LOG_CENTRAL_INVALID:
                LOG_ERR("ZIP", "Invalid central directory record");
                break;
              case XP_BOOK_ZIP_LOG_TARGET_NAME:
                LOG_ERR("ZIP", "Failed to read entry name for %s: expected %u bytes, got %d", ctx.target,
                        static_cast<unsigned>(expected), static_cast<int>(actual));
                break;
              case XP_BOOK_ZIP_LOG_NAME:
                LOG_ERR("ZIP", "Failed to read entry name");
                break;
              case XP_BOOK_ZIP_LOG_LOCAL_READ:
                LOG_ERR("ZIP", "Something went wrong reading the local header");
                break;
              case XP_BOOK_ZIP_LOG_LOCAL_INVALID:
                LOG_ERR("ZIP", "Not a valid zip file header");
                break;
            }
          }};
}
bool ZipFile::loadAllFileStatSlims() {
  IndexContext context{*this};
  auto services = indexServices(context);
  // Keep optional map population outside the callbacks used by streaming readers.
  services.control = [](void* opaque, uint32_t operation, uint32_t argument) -> size_t {
    auto& ctx = *static_cast<IndexContext*>(opaque);
    switch (static_cast<XpBookZipIndexOp>(operation)) {
      case XP_BOOK_ZIP_INDEX_CLEAR:
        ctx.zip.fileStatSlimCache.clear();
        return 1;
      case XP_BOOK_ZIP_INDEX_RESERVE:
        ctx.zip.fileStatSlimCache.reserve(ctx.zip.zipDetails.totalEntries);
        return 1;
      default:
        return indexServices(ctx).control(opaque, operation, argument);
    }
  };
  services.cache = [](void* opaque, uint32_t operation, XpZipEntry* entry, XpBookBorrow name) {
    auto& ctx = *static_cast<IndexContext*>(opaque);
    if (static_cast<XpBookZipCacheOp>(operation) != XP_BOOK_ZIP_CACHE_INSERT)
      return indexServices(ctx).cache(opaque, operation, entry, name);
    ctx.zip.fileStatSlimCache.emplace(
        ctx.name, FileStatSlim{entry->method, entry->compressed_size, entry->uncompressed_size, entry->local_offset});
    return true;
  };
  return xp_book_zip_populate(&services);
}
bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* output) {
  IndexContext context{*this, filename};
  XpZipEntry entry{};
  const auto services = indexServices(context);
  const bool found =
      xp_book_zip_index(&services, XP_BOOK_ZIP_LOOKUP, {reinterpret_cast<const uint8_t*>(filename), strlen(filename)},
                        {reinterpret_cast<const uint8_t*>(lastLookupName.data()), lastLookupName.size()}, &entry);
  if (found) *output = {entry.method, entry.compressed_size, entry.uncompressed_size, entry.local_offset};
  return found;
}
bool ZipFile::enumerate(bool paths, void* opaque, void (*visit)(void*, std::string_view, uint32_t, uint32_t)) {
  IndexContext context{*this, nullptr, opaque, visit};
  const auto services = indexServices(context);
  return xp_book_zip_index(&services, paths ? XP_BOOK_ZIP_PATHS : XP_BOOK_ZIP_ENTRIES, {nullptr, 0}, {nullptr, 0},
                           nullptr);
}

bool ZipFile::loadZipDetails() {
  struct Context {
    ZipFile& zip;
    std::optional<ScopedOpenClose> opened;
    std::unique_ptr<uint8_t[]> buffer;
  } context{*this, {}, {}};
  const XpBookZipDetailServices services{&context,
                                         [](void* opaque) {
                                           auto& ctx = *static_cast<Context*>(opaque);
                                           return bool(ctx.opened.emplace(ctx.zip));
                                         },
                                         [](void* opaque) { return static_cast<Context*>(opaque)->zip.file.size(); },
                                         [](void* opaque, size_t size) {
                                           auto& buffer = static_cast<Context*>(opaque)->buffer;
                                           buffer = makeUniqueNoThrow<uint8_t[]>(size);
                                           return buffer.get();
                                         },
                                         [](void* opaque, uint32_t offset, uint8_t* bytes, size_t size) {
                                           auto& file = static_cast<Context*>(opaque)->zip.file;
                                           return file.seek(offset) && file.read(bytes, size) == static_cast<int>(size);
                                         },
                                         [](void* opaque, const XpZipDetails* value) {
                                           static_cast<Context*>(opaque)->zip.zipDetails = {value->central_offset,
                                                                                            value->entries, true};
                                         },
                                         [](void*, uint32_t event) {
                                           switch (static_cast<XpBookZipDetailDiagnostic>(event)) {
                                             case XP_BOOK_ZIP_DETAIL_LOG_SMALL:
                                               LOG_ERR("ZIP", "File too small to be a valid zip");
                                               break;
                                             case XP_BOOK_ZIP_DETAIL_LOG_SIZE:
                                               LOG_ERR("ZIP", "Invalid zip file size");
                                               break;
                                             case XP_BOOK_ZIP_DETAIL_LOG_ALLOCATION:
                                               LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
                                               break;
                                             case XP_BOOK_ZIP_DETAIL_LOG_SIGNATURE:
                                               LOG_ERR("ZIP", "EOCD signature not found in zip file");
                                               break;
                                           }
                                         }};
  return xp_book_zip_details(&services, zipDetails.isSet);
}

bool ZipFile::open() {
  if (!Storage.openFileForRead("ZIP", filePath, file)) {
    return false;
  }
  return true;
}

bool ZipFile::close() {
  if (file) {
    // Explicit close() required: member variable persists beyond function scope
    file.close();
  }
  xp_book_zip_close(&lastCentralDirPos, &lastCentralDirPosValid, &lastLookupValid);
  return true;
}

bool ZipFile::getInflatedFileSize(const char* filename, size_t* size) {
  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) {
    return false;
  }

  *size = static_cast<size_t>(fileStat.uncompressedSize);
  return true;
}

int ZipFile::fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes) {
  IndexContext context{*this};
  const auto services = indexServices(context);
  struct Batch {
    std::deque<SizeTarget>& targets;
    std::deque<uint32_t>& sizes;
  } batch{targets, sizes};
  return static_cast<int>(xp_book_zip_batch(
      &services, targets.size(), &batch,
      [](void* opaque, size_t i) -> XpBookZipTarget {
        const auto& item = static_cast<Batch*>(opaque)->targets[i];
        return {item.hash, item.len, item.index};
      },
      [](void* opaque, uint16_t i, uint32_t size) {
        auto& sizes = static_cast<Batch*>(opaque)->sizes;
        if (i >= sizes.size()) return false;
        sizes[i] = size;
        return true;
      }));
}

uint8_t* ZipFile::readFileToMemory(const char* filename, size_t* size, const bool trailingNullByte) {
  IndexContext index{*this, filename};
  const auto lookup = indexServices(index);
  ExtractContext context{file, nullptr, filename, {}};
  const auto services = extractServices(context);
  return xp_book_zip_memory(&services, &lookup, {reinterpret_cast<const uint8_t*>(filename), strlen(filename)},
                            {reinterpret_cast<const uint8_t*>(lastLookupName.data()), lastLookupName.size()},
                            trailingNullByte, size);
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  IndexContext index{*this, filename};
  const auto lookup = indexServices(index);
  ExtractContext context{file, &out, filename, {}};
  const auto services = extractServices(context);
  return xp_book_zip_stream(&services, &lookup, {reinterpret_cast<const uint8_t*>(filename), strlen(filename)},
                            {reinterpret_cast<const uint8_t*>(lastLookupName.data()), lastLookupName.size()}, chunkSize,
                            allowEarlyStop);
}
