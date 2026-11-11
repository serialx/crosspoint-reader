#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
struct HalFile {
  const std::vector<uint8_t>& bytes;
  size_t position = 0, reads = 0, seeks = 0;
  bool failSeek = false, shortRead = false, negativeRead = false;
  size_t failAfter = SIZE_MAX, failEvery = 1;
  std::vector<std::array<int64_t, 4>> trace;
  explicit operator bool() const { return !bytes.empty(); }
  bool seek(size_t offset) {
    ++seeks;
    position = offset;
    const bool ok = !failSeek && offset <= bytes.size();
    trace.push_back({0, static_cast<int64_t>(offset), 0, ok});
    return ok;
  }
  int read(uint8_t* out, size_t count) {
    ++reads;
    if (negativeRead || (reads > failAfter && (reads - failAfter) % failEvery == 0)) {
      trace.push_back({1, static_cast<int64_t>(position), static_cast<int64_t>(count), -1});
      return -1;
    }
    const size_t available = position <= bytes.size() ? bytes.size() - position : 0;
    size_t n = std::min(count, available);
    if (shortRead && n) --n;
    trace.push_back({1, static_cast<int64_t>(position), static_cast<int64_t>(count), static_cast<int64_t>(n)});
    if (n) memcpy(out, bytes.data() + position, n);
    position += n;
    return static_cast<int>(n);
  }
};
