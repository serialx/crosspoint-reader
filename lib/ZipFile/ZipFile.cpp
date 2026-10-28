#include "ZipFile.h"

#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

struct ZipInflateCtx {
  HalFile* file = nullptr;
  size_t fileRemaining = 0;
  uint8_t* readBuf = nullptr;
  size_t readBufSize = 0;
};

namespace {
constexpr uint16_t ZIP_METHOD_STORED = 0;
constexpr uint16_t ZIP_METHOD_DEFLATED = 8;
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

size_t zipFillCallback(void* vctx, const uint8_t** data) {
  auto* ctx = static_cast<ZipInflateCtx*>(vctx);
  if (ctx->fileRemaining == 0) return 0;

  const size_t toRead = ctx->fileRemaining < ctx->readBufSize ? ctx->fileRemaining : ctx->readBufSize;
  const int result = ctx->file->read(ctx->readBuf, toRead);
  // HalFile::read() returns a negative int on error. Treat it as end-of-stream
  // rather than letting the negative-to-size_t conversion underflow fileRemaining
  // and report a huge bytesRead, which would have the inflate library read past
  // the end of readBuf.
  if (result < 0) {
    LOG_ERR("ZIP", "Failed to read compressed data: %d", result);
    return 0;
  }
  const size_t bytesRead = static_cast<size_t>(result);
  ctx->fileRemaining -= bytesRead;

  *data = ctx->readBuf;
  return bytesRead;
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

  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;
  const auto dataSize = trailingNullByte ? inflatedDataSize + 1 : inflatedDataSize;
  const auto data = static_cast<uint8_t*>(malloc(dataSize));
  if (data == nullptr) {
    LOG_ERR("ZIP", "Failed to allocate memory for output buffer (%zu bytes)", dataSize);
    return nullptr;
  }

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const size_t dataRead = file.read(data, inflatedDataSize);

    if (dataRead != inflatedDataSize) {
      LOG_ERR("ZIP", "Failed to read data");
      free(data);
      return nullptr;
    }

    // Continue out of block with data set
  } else if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(1024));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      free(data);
      return nullptr;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = 1024;

    // One-shot mode: `data` holds the entire output, so back-references
    // resolve inside it and no 32KB window is allocated.
    InflateStream inflate;
    if (!inflate.init(false)) {
      LOG_ERR("ZIP", "Failed to init inflate stream for %s", filename);
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    inflate.setFill(zipFillCallback, &ctx);

    if (!inflate.read(data, inflatedDataSize)) {
      LOG_ERR("ZIP", "Failed to inflate file");
      free(fileReadBuffer);
      free(data);
      return nullptr;
    }
    free(fileReadBuffer);

    // Continue out of block with data set
  } else {
    LOG_ERR("ZIP", "Unsupported compression method");
    free(data);
    return nullptr;
  }

  if (trailingNullByte) data[inflatedDataSize] = '\0';
  if (size) *size = inflatedDataSize;
  return data;
}

bool ZipFile::readFileToStream(const char* filename, Print& out, const size_t chunkSize, const bool allowEarlyStop) {
  const ScopedOpenClose zip{*this};
  if (!zip) return false;

  FileStatSlim fileStat = {};
  if (!loadFileStatSlim(filename, &fileStat)) return false;

  const long fileOffset = getDataOffset(fileStat);
  if (fileOffset < 0) return false;

  file.seek(fileOffset);
  const auto deflatedDataSize = fileStat.compressedSize;
  const auto inflatedDataSize = fileStat.uncompressedSize;

  if (fileStat.method == ZIP_METHOD_STORED) {
    // no deflation, just read content
    const auto buffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!buffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for buffer");
      return false;
    }

    size_t remaining = inflatedDataSize;
    while (remaining > 0) {
      const size_t dataRead = file.read(buffer, remaining < chunkSize ? remaining : chunkSize);
      if (dataRead == 0) {
        LOG_ERR("ZIP", "Could not read more bytes");
        free(buffer);
        return false;
      }

      if (out.write(buffer, dataRead) != dataRead) {
        free(buffer);
        if (allowEarlyStop) return true;  // sink has what it needs
        LOG_ERR("ZIP", "Failed to write all output bytes to stream");
        return false;
      }
      remaining -= dataRead;
    }

    free(buffer);
    return true;
  }

  if (fileStat.method == ZIP_METHOD_DEFLATED) {
    auto* fileReadBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!fileReadBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for zip file read buffer");
      return false;
    }

    auto* outputBuffer = static_cast<uint8_t*>(malloc(chunkSize));
    if (!outputBuffer) {
      LOG_ERR("ZIP", "Failed to allocate memory for output buffer");
      free(fileReadBuffer);
      return false;
    }

    ZipInflateCtx ctx;
    ctx.file = &file;
    ctx.fileRemaining = deflatedDataSize;
    ctx.readBuf = fileReadBuffer;
    ctx.readBufSize = chunkSize;

    InflateStream inflate;
    if (!inflate.init(true)) {
      LOG_ERR("ZIP", "Failed to init inflate stream for %s", filename);
      free(outputBuffer);
      free(fileReadBuffer);
      return false;
    }
    inflate.setFill(zipFillCallback, &ctx);

    bool success = false;
    size_t totalProduced = 0;

    while (true) {
      size_t produced;
      const InflateStream::Status status = inflate.readAtMost(outputBuffer, chunkSize, &produced);

      totalProduced += produced;
      if (totalProduced > static_cast<size_t>(inflatedDataSize)) {
        LOG_ERR("ZIP", "Decompressed size exceeds expected (%zu > %zu)", totalProduced,
                static_cast<size_t>(inflatedDataSize));
        break;
      }

      if (produced > 0) {
        if (out.write(outputBuffer, produced) != produced) {
          if (allowEarlyStop) {
            success = true;  // sink has what it needs
          } else {
            LOG_ERR("ZIP", "Failed to write all output bytes to stream");
          }
          break;
        }
      }

      if (status == InflateStream::Status::Done) {
        if (totalProduced != static_cast<size_t>(inflatedDataSize)) {
          LOG_ERR("ZIP", "Decompressed size mismatch (expected %zu, got %zu)", static_cast<size_t>(inflatedDataSize),
                  totalProduced);
          break;
        }
        LOG_DBG("ZIP", "Decompressed %d bytes into %d bytes", deflatedDataSize, inflatedDataSize);
        success = true;
        break;
      }

      if (status == InflateStream::Status::Error) {
        LOG_ERR("ZIP", "Decompression failed");
        break;
      }
      // InflateStream::Status::Ok: output buffer full, continue
    }

    free(outputBuffer);
    free(fileReadBuffer);
    return success;  // inflate destructor frees the decompressor state + window
  }

  LOG_ERR("ZIP", "Unsupported compression method");
  return false;
}
