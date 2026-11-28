#pragma once
#include <cstdio>

#include "../../hal_fake_strict.h"
#include "Arduino.h"
#ifdef LIBRARY_HEADER_FAULT_TEST
inline bool headerFailReads = false;
inline unsigned headerFailedReads = 0;
#endif
class HalFile {
 public:
  HalFile() = default;
  ~HalFile() {
    if (hasImpl_) close();
  }
  HalFile(HalFile&& other) noexcept : HalFile() { *this = std::move(other); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile& operator=(HalFile&& other) noexcept {
    if (hasImpl_) close();
    hasImpl_ = std::move(other.hasImpl_);  // The move leaves other without an impl.
    file_ = other.file_;
    other.file_ = nullptr;
    return *this;
  }
  bool open(const char* path) {
    hasImpl_ = true;  // HalStorage installs an impl even when the open fails.
    if (file_) close();
    file_ = std::fopen(path, "rb");
    return file_ != nullptr;
  }
  bool seekSet(size_t pos) {
    hal_fake::requireImpl(hasImpl_, "seekSet", "");
    return file_ && std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  int read(void* out, size_t count) {
    hal_fake::requireImpl(hasImpl_, "read", "");
#ifdef LIBRARY_HEADER_FAULT_TEST
    if (headerFailReads) {
      ++headerFailedReads;
      return 0;
    }
#endif
    return file_ ? static_cast<int>(std::fread(out, 1, count, file_)) : 0;
  }
  size_t fileSize() const {
    hal_fake::requireImpl(hasImpl_, "fileSize", "");
    if (!file_) return 0;
    const auto pos = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const auto size = std::ftell(file_);
    std::fseek(file_, pos, SEEK_SET);
    return size;
  }
  void close() {
    hal_fake::requireImpl(hasImpl_, "close", "");
    if (file_) std::fclose(file_);
    file_ = nullptr;
  }

 private:
  FILE* file_ = nullptr;
  hal_fake::Impl hasImpl_;  // A move leaves the source without an impl.
};
struct HalStorage {
  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path); }
};
inline HalStorage Storage;
