#pragma once

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include "../../hal_fake_strict.h"

namespace HalFileTest {
inline bool (*allowSeek)(const char*, size_t) = nullptr;
inline bool failNextWrite = false;
inline bool failNextClose = false;
// Optional host fault hooks. Null hooks preserve ordinary filesystem behavior.
inline bool (*allowOpen)(const char*, const char*) = nullptr;
inline size_t (*readLimit)(const char*, size_t, size_t) = nullptr;
inline size_t (*writeLimit)(const char*, size_t, size_t) = nullptr;
}  // namespace HalFileTest

constexpr int O_WRITE = 1;
constexpr int O_CREAT = 2;
constexpr int O_TRUNC = 4;

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(const char* path) { open(path, "wb"); }
  ~HalFile() {
    if (impl_) close();
  }
  HalFile(HalFile&& other) noexcept
      : file_(std::exchange(other.file_, nullptr)),
        path_(std::move(other.path_)),
        writeBytesLeft_(other.writeBytesLeft_),
        limitedWrites_(other.limitedWrites_),
        impl_(std::move(other.impl_)) {}
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;
  HalFile& operator=(HalFile&& other) noexcept {
    if (file_) close();
    file_ = std::exchange(other.file_, nullptr);
    path_ = std::move(other.path_);
    writeBytesLeft_ = other.writeBytesLeft_;
    limitedWrites_ = other.limitedWrites_;
    impl_ = std::move(other.impl_);
    return *this;
  }

  bool open(const char* path, const char* mode) {
    impl_.install();     // HalStorage installs an impl for every open attempt, success or not.
    if (file_) close();  // release the previous file; the impl above keeps this legal
    path_ = path;
    if (HalFileTest::allowOpen && !HalFileTest::allowOpen(path, mode)) return false;
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }
  int available() const {
    hal_fake::requireImpl(impl_, "available", path_.c_str());
    return file_ ? static_cast<int>(size() - position()) : 0;
  }
  int read() {
    uint8_t byte;
    return read(&byte, 1) == 1 ? byte : -1;
  }
  size_t read(void* buffer, size_t count) {
    hal_fake::requireImpl(impl_, "read", path_.c_str());
    if (HalFileTest::readLimit) count = HalFileTest::readLimit(path_.c_str(), position(), count);
    return file_ ? std::fread(buffer, 1, count, file_) : 0;
  }
  size_t write(const void* buffer, size_t count) {
    hal_fake::requireImpl(impl_, "write", path_.c_str());
    if (HalFileTest::failNextWrite) {
      HalFileTest::failNextWrite = false;
      return 0;
    }
    if (HalFileTest::writeLimit) count = HalFileTest::writeLimit(path_.c_str(), position(), count);
    // Default streams are bounded too: never silently exhaust the narrowed budget.
    if (!limitedWrites_ && count > writeBytesLeft_) std::abort();
    const size_t allowed = count < writeBytesLeft_ ? count : writeBytesLeft_;
    const size_t written = file_ ? std::fwrite(buffer, 1, allowed, file_) : 0;
    writeBytesLeft_ -= written;
    return written;
  }
  size_t write(uint8_t byte) { return write(&byte, 1); }
  bool flush() {
    hal_fake::requireImpl(impl_, "flush", path_.c_str());
    return file_ && std::fflush(file_) == 0;
  }
  bool seek(size_t pos) {
    hal_fake::requireImpl(impl_, "seek", path_.c_str());
    return (!HalFileTest::allowSeek || HalFileTest::allowSeek(path_.c_str(), pos)) && file_ &&
           std::fseek(file_, static_cast<long>(pos), SEEK_SET) == 0;
  }
  uint64_t fileSize64() const { return size(); }
  bool seek64(uint64_t pos) { return pos <= SIZE_MAX && seek(static_cast<size_t>(pos)); }
  bool seekCur(size_t offset) {
    hal_fake::requireImpl(impl_, "seekCur", path_.c_str());
    return file_ && std::fseek(file_, static_cast<long>(offset), SEEK_CUR) == 0;
  }
  bool close() {
    hal_fake::requireImpl(impl_, "close", path_.c_str());
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    if (HalFileTest::failNextClose) {
      HalFileTest::failNextClose = false;
      return false;
    }
    return ok;
  }
  bool isOpen() const { return file_ != nullptr; }
  explicit operator bool() const { return isOpen(); }
  size_t position() const {
    hal_fake::requireImpl(impl_, "position", path_.c_str());
    return file_ ? static_cast<size_t>(std::ftell(file_)) : 0;
  }
  size_t size() const {
    hal_fake::requireImpl(impl_, "size", path_.c_str());
    if (!file_) return 0;
    const long offset = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long end = std::ftell(file_);
    std::fseek(file_, offset, SEEK_SET);
    return end > 0 ? static_cast<size_t>(end) : 0;
  }

  size_t write(const uint8_t* buffer, size_t count) { return write(static_cast<const void*>(buffer), count); }
  void limitWritesTo(size_t count) {
    // This host-only fault API supports at most UINT32_MAX bytes; keep size-recording goldens stable.
    if (count > UINT32_MAX) std::abort();
    writeBytesLeft_ = static_cast<uint32_t>(count);
    limitedWrites_ = true;
  }

 private:
  std::FILE* file_ = nullptr;
  std::string path_;
  // Reuse 64-bit ABI padding; contract tests pin sizeof against the size-recording goldens.
  uint32_t writeBytesLeft_ = UINT32_MAX;
  bool limitedWrites_ = false;
  hal_fake::Impl impl_;  // Set by every open attempt, even a failed one with an empty path.
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage instance;
    return instance;
  }
  int readOpens = 0;
  std::string dictionaryTempPath;
  std::string resolvePath(const std::string& path) const {
    return path == "/.crosspoint/dicthtml.tmp" && !dictionaryTempPath.empty() ? dictionaryTempPath : path;
  }
  HalFile open(const char* path, int) { return HalFile(resolvePath(path).c_str()); }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    readOpens++;
    return file.open(resolvePath(path).c_str(), "rb");
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
  bool exists(const char* path) const {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
  }
  bool remove(const std::string& path) { return std::remove(resolvePath(path).c_str()) == 0; }
  bool rename(const char* from, const char* to) { return std::rename(from, to) == 0; }
  bool mkdir(const char* path) {
    return std::filesystem::create_directories(path) || std::filesystem::is_directory(path);
  }
};

#define Storage HalStorage::getInstance()

inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
