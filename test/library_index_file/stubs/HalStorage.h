#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "../../hal_fake_strict.h"

class HalFile {
  friend class HalStorage;

 public:
  bool close() {
    hal_fake::requireImpl(hasImpl, "close");
    open = false;
    return true;
  }

  bool isOpen() const { return hasImpl && open; }
  int read(void* dst, size_t len) {
    hal_fake::requireImpl(hasImpl, "read");
    if (!isOpen() || !data || position + len > data->size()) return 0;
    std::memcpy(dst, data->data() + position, len);
    position += len;
    return static_cast<int>(len);
  }
  uint64_t fileSize64() {
    hal_fake::requireImpl(hasImpl, "fileSize64");
    return data ? data->size() : 0;
  }
  bool seekSet(size_t offset) {
    hal_fake::requireImpl(hasImpl, "seekSet");
    if (!isOpen() || !data || offset > data->size()) return false;
    position = offset;
    return true;
  }

 private:
  hal_fake::Impl hasImpl;  // A move leaves the source without an impl.
  bool open = false;
  const std::vector<uint8_t>* data = nullptr;
  size_t position = 0;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) {
    file.hasImpl = true;
    file.open = *path && path == filePath;
    file.data = file.open ? &fileData : nullptr;
    file.position = 0;
    return file.open;
  }

  void setFile(const std::string& path, std::vector<uint8_t> data) {
    filePath = path;
    fileData = std::move(data);
  }

  void clearFile() {
    filePath.clear();
    fileData.clear();
  }

 private:
  std::string filePath;
  std::vector<uint8_t> fileData;
};

#define Storage HalStorage::getInstance()
