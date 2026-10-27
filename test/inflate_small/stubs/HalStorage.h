#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

class HalFile {
 public:
  std::vector<uint8_t> bytes;
  size_t cursor = 0;
  size_t position() const { return cursor; }
  size_t fileSize() const { return bytes.size(); }
  bool seekSet(size_t offset) {
    if (offset > bytes.size()) return false;
    cursor = offset;
    return true;
  }
  int read(void* output, size_t length) {
    const size_t count = std::min(length, bytes.size() - cursor);
    if (count) memcpy(output, bytes.data() + cursor, count);
    cursor += count;
    return static_cast<int>(count);
  }
  int read() {
    if (cursor == bytes.size()) return -1;
    return bytes[cursor++];
  }
  size_t write(const uint8_t* data, size_t size) {
    bytes.insert(bytes.end(), data, data + size);
    return size;
  }
};
struct HalStorage {
  bool openFileForRead(const char*, const char* path, HalFile& file) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    file.bytes.assign(std::istreambuf_iterator<char>(input), {});
    file.cursor = 0;
    return true;
  }
};
inline HalStorage Storage;
