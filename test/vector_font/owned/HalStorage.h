#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
struct HalFile {
  const std::vector<uint8_t>& bytes;
  size_t position = 0, reads = 0, seeks = 0;
  bool failSeek = false, shortRead = false;
  explicit operator bool() const { return !bytes.empty(); }
  bool seek(size_t offset) {
    ++seeks;
    position = offset;
    return !failSeek && offset <= bytes.size();
  }
  int read(uint8_t* out, size_t count) {
    ++reads;
    size_t n = count < bytes.size() - position ? count : bytes.size() - position;
    if (shortRead && n) --n;
    memcpy(out, bytes.data() + position, n);
    position += n;
    return static_cast<int>(n);
  }
};
