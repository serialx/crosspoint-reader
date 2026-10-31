#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <optional>

namespace {
enum class DirectoryStep : int32_t { Invalid = -1, Done = 0, Entry = 1, Wrap = 2 };
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
  FileStatSlim* output = nullptr;
  void* visitContext = nullptr;
  void (*visit)(void*, std::string_view, uint32_t, uint32_t) = nullptr;
  std::optional<ScopedOpenClose> opened;
  XpZipWalker walker{};
  char name[256];
  IndexContext(ZipFile& zip, const char* target = nullptr, FileStatSlim* output = nullptr, void* visitContext = nullptr,
               void (*visit)(void*, std::string_view, uint32_t, uint32_t) = nullptr)
      : zip(zip), target(target), output(output), visitContext(visitContext), visit(visit) {}
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
                return bool(ctx.opened.emplace(zip));
              case XP_BOOK_ZIP_INDEX_DETAILS:
                return zip.loadZipDetails();
              case XP_BOOK_ZIP_INDEX_CENTRAL:
                return zip.zipDetails.centralDirOffset;
              case XP_BOOK_ZIP_INDEX_START:
                xp_zip_begin(zip.zipDetails.centralDirOffset, argument, &ctx.walker);
                return 1;
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
          [](void* opaque, XpZipEntry* entry) {
            auto& ctx = *static_cast<IndexContext*>(opaque);
            return ctx.zip.readDirectoryEntry(ctx.walker, *entry);
          },
          [](void* opaque, const XpZipEntry* entry, bool target) -> XpBookBorrow {
            auto& ctx = *static_cast<IndexContext*>(opaque);
            if (!ctx.zip.readEntryName(*entry, ctx.name, target ? ctx.target : nullptr)) return {nullptr, 0};
            return {reinterpret_cast<const uint8_t*>(ctx.name), entry->name.length};
          },
          [](void* opaque, uint32_t operation, const XpZipEntry* entry, XpBookBorrow name) {
            auto& ctx = *static_cast<IndexContext*>(opaque);
            auto& zip = ctx.zip;
            const auto path = std::string_view(reinterpret_cast<const char*>(name.data), name.len);

            switch (static_cast<XpBookZipCacheOp>(operation)) {
              case XP_BOOK_ZIP_CACHE_FIND: {
                const auto found = zip.fileStatSlimCache.find(ctx.target);
                if (found == zip.fileStatSlimCache.end()) return false;
                *ctx.output = found->second;
                return true;
              }
              case XP_BOOK_ZIP_CACHE_LAST:
                *ctx.output = zip.lastLookup;
                return true;
              case XP_BOOK_ZIP_CACHE_REMEMBER:
                *ctx.output = {entry->method, entry->compressed_size, entry->uncompressed_size, entry->local_offset};
                zip.lastLookupName.assign(path);
                zip.lastLookup = *ctx.output;
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
  services.cache = [](void* opaque, uint32_t operation, const XpZipEntry* entry, XpBookBorrow name) {
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
  IndexContext context{*this, filename, output};
  const auto services = indexServices(context);
  return xp_book_zip_index(&services, XP_BOOK_ZIP_LOOKUP,
                           {reinterpret_cast<const uint8_t*>(filename), strlen(filename)},
                           {reinterpret_cast<const uint8_t*>(lastLookupName.data()), lastLookupName.size()});
}
bool ZipFile::enumerate(bool paths, void* opaque, void (*visit)(void*, std::string_view, uint32_t, uint32_t)) {
  IndexContext context{*this, nullptr, nullptr, opaque, visit};
  const auto services = indexServices(context);
  return xp_book_zip_index(&services, paths ? XP_BOOK_ZIP_PATHS : XP_BOOK_ZIP_ENTRIES, {nullptr, 0}, {nullptr, 0});
}

bool ZipFile::readDirectoryEntry(XpZipWalker& walker, XpZipEntry& entry) {
  uint8_t header[46];
  XpZipRead request;
  while (xp_zip_request(&walker, &request) == 1) {
    if (!file.seek(request.offset)) {
      LOG_ERR("ZIP", "Failed to seek central directory");
      return false;
    }
    const int read = file.read(header, request.length);
    const auto step =
        static_cast<DirectoryStep>(xp_zip_next(&walker, header, read < 0 ? 0 : static_cast<size_t>(read), &entry));
    switch (step) {
      case DirectoryStep::Entry:
        return true;
      case DirectoryStep::Wrap:
        continue;
      case DirectoryStep::Invalid:
        LOG_ERR("ZIP", "Invalid central directory record");
        return false;
      case DirectoryStep::Done:
        return false;
    }
  }
  return false;
}

bool ZipFile::readEntryName(const XpZipEntry& entry, char* name, const char* target) {
  const int count = file.read(name, entry.name.length);
  if (count != static_cast<int>(entry.name.length)) {
    if (target)
      LOG_ERR("ZIP", "Failed to read entry name for %s: expected %u bytes, got %d", target,
              static_cast<unsigned>(entry.name.length), count);
    else
      LOG_ERR("ZIP", "Failed to read entry name");
    return false;
  }
  name[entry.name.length] = '\0';
  return true;
}

long ZipFile::getDataOffset(const FileStatSlim& fileStat) {
  const ScopedOpenClose zip{*this};
  if (!zip) return -1;

  XpZipRead request;
  xp_zip_local_request(fileStat.localHeaderOffset, &request);
  uint8_t header[30];
  if (!file.seek(request.offset) || file.read(header, request.length) != static_cast<int>(request.length)) {
    LOG_ERR("ZIP", "Something went wrong reading the local header");
    return -1;
  }
  uint64_t offset;
  if (xp_zip_data_offset(fileStat.localHeaderOffset, header, sizeof(header), &offset) != 0) {
    LOG_ERR("ZIP", "Not a valid zip file header");
    return -1;
  }
  return static_cast<long>(offset);
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
  const ScopedOpenClose zip{*this};
  if (!zip) return nullptr;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return nullptr;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return nullptr;

  file.seek(fileOffset);

  ExtractContext context{file, nullptr, filename, {}};
  const auto services = extractServices(context);
  return xp_book_zip_memory(&services, fileStat.method, fileStat.compressedSize, fileStat.uncompressedSize,
                            trailingNullByte, size);
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  file.seek(fileOffset);
  ExtractContext context{file, &out, filename, {}};
  const auto services = extractServices(context);
  return xp_book_zip_stream(&services, fileStat.method, fileStat.compressedSize, fileStat.uncompressedSize, chunkSize,
                            allowEarlyStop);
}
