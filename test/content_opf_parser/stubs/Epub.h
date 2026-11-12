#pragma once

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "../../hal_fake_strict.h"

class HalFile {
 public:
  explicit operator bool() const { return initialized_ && open_; }
  void close() {
    hal_fake::requireImpl(initialized_, "close");
    open_ = false;
  }
  bool seek(size_t position) {
    hal_fake::requireImpl(initialized_, "seek");
    position_ = position;
    return true;
  }
  size_t position() const {
    hal_fake::requireImpl(initialized_, "position");
    return position_;
  }
  int available() const {
    hal_fake::requireImpl(initialized_, "available");
    return 0;
  }
  size_t size() const {
    hal_fake::requireImpl(initialized_, "size");
    return position_;
  }
  size_t write(const uint8_t*, size_t size) {
    hal_fake::requireImpl(initialized_, "write");
    position_ += size;
    return size;
  }
  int read(uint8_t*, size_t) {
    hal_fake::requireImpl(initialized_, "read");
    return 0;
  }
  void markOpen() {
    initialized_ = true;
    open_ = true;
  }

 private:
  bool open_ = false;
  hal_fake::Impl initialized_;  // A move leaves the source without an impl.
  size_t position_ = 0;
};

struct TestStorage {
  int writeOpens = 0;
  int readOpens = 0;

  bool openFileForWrite(const char*, const std::string&, HalFile& file) {
    writeOpens++;
    file.markOpen();  // HalStorage installs an impl even when the open fails.
    return true;
  }
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    readOpens++;
    file.markOpen();  // HalStorage installs an impl even when the open fails.
    return true;
  }
  bool exists(const char*) const { return false; }
  bool remove(const char*) { return true; }
};

inline TestStorage Storage;
