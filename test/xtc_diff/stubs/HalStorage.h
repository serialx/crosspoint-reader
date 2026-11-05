#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
inline std::vector<uint8_t> xtcTestFile;
class HalFile {
 public:
  bool seek64(uint64_t offset) {
    if (!opened || offset > xtcTestFile.size()) return false;
    position = offset;
    return true;
  }
  bool seek(uint32_t offset) { return seek64(offset); }
  size_t read(void* out, size_t size) {
    if (!opened) return 0;
    size = std::min(size, xtcTestFile.size() - position);
    if (size) memcpy(out, xtcTestFile.data() + position, size);
    position += size;
    return size;
  }
  uint64_t fileSize64() const { return xtcTestFile.size(); }
  bool isOpen() const { return opened; }
  void close() { opened = false; }
  void open() {
    opened = true;
    position = 0;
  }

 private:
  size_t position = 0;
  bool opened = false;
};
struct HalStorage {
  bool openFileForRead(const char*, const char*, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
