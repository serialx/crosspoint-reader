#include "KOReaderDocumentId.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

#include "SyncBridge.h"

std::string KOReaderDocumentId::calculateFromFilename(const std::string& filePath) {
  const auto view = xp_sync_filename(syncSpan(filePath));
  const std::string filename(reinterpret_cast<const char*>(view.data), view.len);
  if (filename.empty()) {
    return "";
  }

  MD5Builder md5;
  md5.begin();
  md5.add(filename.c_str());
  md5.calculate();

  std::string result = md5.toString().c_str();
  LOG_DBG("KODoc", "Filename hash: %s (from '%s')", result.c_str(), filename.c_str());
  return result;
}

std::string KOReaderDocumentId::calculate(const std::string& filePath) {
  HalFile file;
  if (!Storage.openFileForRead("KODoc", filePath, file)) {
    LOG_DBG("KODoc", "Failed to open file: %s", filePath.c_str());
    return "";
  }

  const size_t fileSize = file.fileSize();
  LOG_DBG("KODoc", "Calculating hash for file: %s (size: %zu)", filePath.c_str(), fileSize);

  // Initialize MD5 builder
  MD5Builder md5;
  md5.begin();

  // Buffer for reading chunks
  uint8_t buffer[CHUNK_SIZE];
  struct Context {
    HalFile& file;
    MD5Builder& md5;
    uint8_t* buffer;
  } context{file, md5, buffer};
  const size_t totalBytesRead =
      xp_sync_samples(&context, fileSize, [](void* raw, size_t offset, size_t count) -> size_t {
        auto& c = *static_cast<Context*>(raw);
        if (!c.file.seekSet(offset)) {
          LOG_DBG("KODoc", "Failed to seek to offset %zu", offset);
          return 0;
        }
        const size_t read = c.file.read(c.buffer, count);
        if (read > 0) c.md5.add(c.buffer, read);
        return read;
      });

  // Calculate final hash
  md5.calculate();
  std::string result = md5.toString().c_str();

  LOG_DBG("KODoc", "Hash calculated: %s (from %zu bytes)", result.c_str(), totalBytesRead);

  return result;
}
