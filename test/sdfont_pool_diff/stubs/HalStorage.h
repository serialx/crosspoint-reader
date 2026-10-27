#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
inline std::vector<uint8_t> sdFontTestFile;
inline size_t sdFontTestReads = 0;
inline uint64_t sdTrace = 14695981039346656037ULL;
inline void sdEvent(uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    sdTrace = (sdTrace ^ (v & 255)) * 1099511628211ULL;
    v >>= 8;
  }
}
struct SdFontTestEsp {
  size_t freeHeap = 200 * 1024, largestBlock = 200 * 1024;
  bool creditArrayFrees = false;
  size_t getFreeHeap() const { return freeHeap; }
  size_t getMaxAllocHeap() const { return largestBlock; }
};
inline SdFontTestEsp ESP;
inline uint32_t millis() { return 0; }
class HalFile {
 public:
  bool seekSet(size_t position) {
    sdEvent(1);
    sdEvent(position);
    position_ = position;
    return opened_ && position <= sdFontTestFile.size();
  }
  int read(void* output, size_t count) {
    sdEvent(2);
    sdEvent(position_);
    sdEvent(count);
    if (!opened_ || position_ > sdFontTestFile.size()) return 0;
    count = std::min(count, sdFontTestFile.size() - position_);
    std::memcpy(output, sdFontTestFile.data() + position_, count);
    position_ += count;
    ++sdFontTestReads;
    return static_cast<int>(count);
  }
  void close() { opened_ = false; }
  size_t fileSize() const { return sdFontTestFile.size(); }
  void open() {
    opened_ = true;
    position_ = 0;
  }

 private:
  size_t position_ = 0;
  bool opened_ = false;
};
class HalStorage {
 public:
  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
