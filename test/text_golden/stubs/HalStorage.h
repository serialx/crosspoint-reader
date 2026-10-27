#pragma once
#include <cstdio>

#include "Arduino.h"
class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile& operator=(HalFile&& other) noexcept {
    close();
    file_ = other.file_;
    other.file_ = nullptr;
    return *this;
  }
  bool open(const char* path) {
    close();
    file_ = std::fopen(path, "rb");
    return file_ != nullptr;
  }
  bool seekSet(size_t pos) { return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0; }
  int read(void* out, size_t count) { return file_ ? static_cast<int>(std::fread(out, 1, count, file_)) : 0; }
  size_t fileSize() const {
    if (!file_) return 0;
    const auto pos = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const auto size = std::ftell(file_);
    std::fseek(file_, pos, SEEK_SET);
    return size;
  }
  void close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
  }

 private:
  FILE* file_ = nullptr;
};
struct HalStorage {
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path); }
};
inline HalStorage Storage;
