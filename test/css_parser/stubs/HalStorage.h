#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../../hal_fake_strict.h"

namespace css_io_test {
inline size_t readLimit = SIZE_MAX, writeLimit = SIZE_MAX;
inline int readsBeforeStop = -1;
inline std::vector<std::pair<std::string, std::string>> failedRenames;
inline std::vector<std::string>* trace = nullptr;
inline void record(std::string value) {
  if (trace) trace->push_back(std::move(value));
}
inline void reset() {
  readLimit = writeLimit = SIZE_MAX;
  readsBeforeStop = -1;
  failedRenames.clear();
  trace = nullptr;
}
struct Trace {
  std::vector<std::string> events;
  Trace() { trace = &events; }
  ~Trace() { trace = nullptr; }
};
}  // namespace css_io_test

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() {
    if (hasImpl_) close();
  }

  HalFile(HalFile&& other) noexcept { *this = std::move(other); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile& operator=(HalFile&& other) noexcept {
    if (file_) close();
    file_ = std::exchange(other.file_, nullptr);
    hasImpl_ = std::move(other.hasImpl_);
    return *this;
  }

  bool open(const char* path, const char* mode) {
    hasImpl_ = true;  // HalStorage installs an impl even when the open fails.
    close();
    css_io_test::record(std::string("open:") + mode);
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }

  int available() const {
    hal_fake::requireImpl(hasImpl_, "available", "");
    if (!file_) return 0;
    const long position = std::ftell(file_);
    if (position < 0 || std::fseek(file_, 0, SEEK_END) != 0) return 0;
    const long end = std::ftell(file_);
    std::fseek(file_, position, SEEK_SET);
    return end >= position ? static_cast<int>(end - position) : 0;
  }

  int read(void* buffer, size_t count) {
    hal_fake::requireImpl(hasImpl_, "read", "");
    if (!file_) return -1;
    css_io_test::record("read:" + std::to_string(count));
    if (css_io_test::readsBeforeStop == 0) return 0;
    if (css_io_test::readsBeforeStop > 0) --css_io_test::readsBeforeStop;
    return static_cast<int>(std::fread(buffer, 1, std::min(count, css_io_test::readLimit), file_));
  }

  size_t write(const void* buffer, size_t count) {
    hal_fake::requireImpl(hasImpl_, "write", "");
    css_io_test::record("write:" + std::to_string(count));
    return file_ ? std::fwrite(buffer, 1, std::min(count, css_io_test::writeLimit), file_) : 0;
  }

  size_t write(uint8_t byte) { return write(&byte, 1); }

  bool seekCur(size_t count) {
    hal_fake::requireImpl(hasImpl_, "seekCur", "");
    return file_ && std::fseek(file_, static_cast<long>(count), SEEK_CUR) == 0;
  }

  bool close() {
    hal_fake::requireImpl(hasImpl_, "close", "");
    if (!file_) return false;
    css_io_test::record("close");
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }

  explicit operator bool() const { return file_ != nullptr; }

 private:
  std::FILE* file_ = nullptr;
  hal_fake::Impl hasImpl_;  // Stands in for HalFile::impl: a move leaves the source without one.
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }

  bool exists(const char* path) const {
    css_io_test::record("exists:" + std::string(path));
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
  }

  bool remove(const char* path) {
    css_io_test::record("remove:" + std::string(path));
    return std::remove(path) == 0;
  }
  bool rename(const char* from, const char* to) {
    css_io_test::record("rename:" + std::string(from) + ":" + to);
    auto& failures = css_io_test::failedRenames;
    if (!failures.empty() && failures.front() == std::make_pair(std::string(from), std::string(to))) {
      failures.erase(failures.begin());
      return false;
    }
    if (from == failedRenameFrom_ && to == failedRenameTo_) {
      clearFailures();
      return false;
    }
    return std::rename(from, to) == 0;
  }

  void failNextRename(std::string from, std::string to) {
    failedRenameFrom_ = std::move(from);
    failedRenameTo_ = std::move(to);
  }

  void clearFailures() {
    failedRenameFrom_.clear();
    failedRenameTo_.clear();
  }

  bool openFileForRead(const char*, const char* path, HalFile& file) { return file.open(path, "rb"); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const char* path, HalFile& file) { return file.open(path, "wb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }

 private:
  std::string failedRenameFrom_;
  std::string failedRenameTo_;
};

#define Storage HalStorage::getInstance()
