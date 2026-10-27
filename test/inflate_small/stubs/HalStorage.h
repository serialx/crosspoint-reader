#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

constexpr int O_WRITE = 1, O_CREAT = 2, O_TRUNC = 4;

struct ReadFault {
  std::string path;
  size_t begin = 0, end = 0, failAt = 0, maxRead = 0;
  size_t shortReads = 0, failures = 0;
};
inline ReadFault readFault;

class HalFile {
 public:
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  std::string path;
  std::vector<uint8_t>* backing = nullptr;
  explicit operator bool() const { return true; }
  void close() {}
  size_t position() const { return cursor; }
  size_t fileSize() const { return bytes.size(); }
  bool seekSet(size_t offset) {
    if (offset > bytes.size()) return false;
    cursor = offset;
    return true;
  }
  int read(void* output, size_t length) {
    size_t count = std::min(length, bytes.size() - cursor);
    if (path == readFault.path && cursor >= readFault.begin && cursor < readFault.end && readFault.maxRead) {
      if (cursor >= readFault.failAt) {
        ++readFault.failures;
        return -1;
      }
      count = std::min({count, readFault.maxRead, readFault.failAt - cursor});
      if (count && count < length) ++readFault.shortReads;
    }
    if (count) memcpy(output, bytes.data() + cursor, count);
    cursor += count;
    return static_cast<int>(count);
  }
  int read() {
    if (cursor == bytes.size()) return -1;
    return bytes[cursor++];
  }
  size_t write(const uint8_t* data, size_t size) {
    auto& target = backing ? *backing : bytes;
    target.insert(target.end(), data, data + size);
    return size;
  }
};
struct HalStorage {
  std::string temporaryPath;
  std::vector<uint8_t> temporary;
  size_t temporaryReads = 0, truncations = 0;
  HalFile open(const char* path, int flags) {
    temporaryPath = path;
    if (flags & O_TRUNC) {
      temporary.clear();
      ++truncations;
    }
    HalFile file;
    file.backing = &temporary;
    return file;
  }
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    if (path == temporaryPath) {
      file.bytes = temporary;
      file.cursor = 0;
      ++temporaryReads;
      return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    file.bytes.assign(std::istreambuf_iterator<char>(input), {});
    file.cursor = 0;
    file.path = path;
    return true;
  }
};
inline HalStorage Storage;
