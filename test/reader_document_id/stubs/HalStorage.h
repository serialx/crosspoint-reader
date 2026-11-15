#pragma once
#include "Trace.h"
class HalFile {
  bool open_ = false;

 public:
  HalFile() { document_test::event("construct"); }
  ~HalFile() {
    if (open_) document_test::event("close");
    document_test::event("destroy");
  }
  void opened() { open_ = true; }
  size_t fileSize() {
    assert(open_);
    document_test::event("size");
    return document_test::size;
  }
  bool seekSet(size_t offset) {
    assert(open_);
    ++document_test::sample;
    document_test::event("seek " + std::to_string(offset));
    if (document_test::sample == document_test::seekFail) return false;
    document_test::offset = uint32_t(offset);
    return true;
  }
  int read(void* data, size_t count) {
    assert(open_ && count <= 1024);
    document_test::event("read " + std::to_string(count));
    if (document_test::sample == document_test::readFail) return -1;
    if (document_test::sample == document_test::readZero) return 0;
    if (document_test::sample == document_test::readShort) count /= 2;
    count = std::min<size_t>(count, document_test::size - document_test::offset);
    for (size_t i = 0; i < count; ++i) static_cast<uint8_t*>(data)[i] = document_test::byte(document_test::offset + i);
    document_test::offset += count;
    return int(count);
  }
};
struct HalStorage {
  bool openFileForRead(const char* origin, const char* path, HalFile& file) {
    assert(std::string(origin) == "KODoc");
    document_test::event("open " + std::string(path));
    if (document_test::openFail) {
      document_test::log(origin, "Failed to open file for reading: %s", path);
      return false;
    }
    file.opened();
    return true;
  }
  bool openFileForRead(const char* origin, const std::string& path, HalFile& file) {
    return openFileForRead(origin, path.c_str(), file);
  }
};
inline HalStorage Storage;
