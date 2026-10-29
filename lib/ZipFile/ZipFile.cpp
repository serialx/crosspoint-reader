#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

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

bool ZipFile::loadAllFileStatSlims() {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  XpZipWalker walker;
  xp_zip_begin(zipDetails.centralDirOffset, zipDetails.centralDirOffset, &walker);
  XpZipEntry entry;
  char itemName[256];
  fileStatSlimCache.clear();
  fileStatSlimCache.reserve(zipDetails.totalEntries);
  while (readDirectoryEntry(walker, entry)) {
    if (entry.name.length >= sizeof(itemName)) continue;
    if (!readEntryName(entry, itemName)) break;
    fileStatSlimCache.emplace(
        itemName, FileStatSlim{entry.method, entry.compressed_size, entry.uncompressed_size, entry.local_offset});
  }

  // Set cursor to start of central directory for sequential access
  lastCentralDirPos = zipDetails.centralDirOffset;
  lastCentralDirPosValid = true;

  return true;
}

bool ZipFile::loadFileStatSlim(const char* filename, FileStatSlim* fileStat) {
  if (!fileStatSlimCache.empty()) {
    const auto it = fileStatSlimCache.find(filename);
    if (it != fileStatSlimCache.end()) {
      *fileStat = it->second;
      return true;
    }
    return false;
  }

  if (lastLookupValid && lastLookupName == filename) {
    *fileStat = lastLookup;
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  if (!loadZipDetails()) return false;

  const uint32_t startPos = lastCentralDirPosValid ? lastCentralDirPos : zipDetails.centralDirOffset;
  XpZipWalker walker;
  xp_zip_begin(zipDetails.centralDirOffset, startPos, &walker);
  XpZipEntry entry;
  const size_t targetLen = strlen(filename);
  char itemName[256];
  while (readDirectoryEntry(walker, entry)) {
    if (entry.name.length != targetLen || entry.name.length >= sizeof(itemName)) continue;
    if (!readEntryName(entry, itemName, filename)) break;
    if (xp_zip_name_matches(reinterpret_cast<const uint8_t*>(itemName), entry.name.length,
                            reinterpret_cast<const uint8_t*>(filename), targetLen) == 1) {
      *fileStat = {entry.method, entry.compressed_size, entry.uncompressed_size, entry.local_offset};
      lastCentralDirPos = entry.next_offset;
      lastCentralDirPosValid = true;
      lastLookupName.assign(filename, targetLen);
      lastLookup = *fileStat;
      lastLookupValid = true;
      return true;
    }
  }
  return false;
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
  if (zipDetails.isSet) {
    return true;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  XpZipRead request;
  const size_t fileSize = file.size();
  if (fileSize < 22) {
    LOG_ERR("ZIP", "File too small to be a valid zip");
    return false;
  }
  if (fileSize > UINT32_MAX || xp_zip_eocd_request(static_cast<uint32_t>(fileSize), &request) != 0) {
    LOG_ERR("ZIP", "Invalid zip file size");
    return false;
  }
  const auto buffer = makeUniqueNoThrow<uint8_t[]>(request.length);
  if (!buffer) {
    LOG_ERR("ZIP", "Failed to allocate memory for EOCD scan buffer");
    return false;
  }
  XpZipDetails details;
  const bool found = file.seek(request.offset) &&
                     file.read(buffer.get(), request.length) == static_cast<int>(request.length) &&
                     xp_zip_eocd(buffer.get(), request.length, &details) == 0;
  if (!found) {
    LOG_ERR("ZIP", "EOCD signature not found in zip file");
    return false;
  }
  zipDetails = {details.central_offset, details.entries, true};
  return true;
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
  lastCentralDirPos = 0;
  lastCentralDirPosValid = false;
  // the archive may change before the next open
  lastLookupValid = false;
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
  if (targets.empty()) {
    return 0;
  }

  const ScopedOpenClose zip{*this};
  if (!zip) return 0;

  if (!loadZipDetails()) return 0;

  XpZipWalker walker;
  xp_zip_begin(zipDetails.centralDirOffset, zipDetails.centralDirOffset, &walker);
  XpZipEntry entry;
  int matched = 0;
  const int targetCount = static_cast<int>(targets.size());
  char itemName[256];
  while (readDirectoryEntry(walker, entry)) {
    if (entry.name.length >= sizeof(itemName)) continue;
    if (!readEntryName(entry, itemName)) break;
    const uint64_t hash = fnvHash64(itemName, entry.name.length);
    const SizeTarget key = {hash, static_cast<uint16_t>(entry.name.length), 0};
    auto it = std::lower_bound(targets.begin(), targets.end(), key, [](const SizeTarget& a, const SizeTarget& b) {
      return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
    });
    while (it != targets.end() && it->hash == hash && it->len == entry.name.length) {
      if (it->index < sizes.size()) {
        sizes[it->index] = entry.uncompressed_size;
        matched++;
      }
      ++it;
    }
    if (matched >= targetCount) break;
  }

  return matched;
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
