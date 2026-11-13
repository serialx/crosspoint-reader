#pragma once
#include <Print.h>
#include <fcntl.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
namespace protection_test {
inline std::vector<std::string> trace;
inline size_t steps = 0, failAt = 0;
inline bool shortRead = false;
// Small segments model a near-4-GiB archive without allocating or writing that size.
inline std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segments;
inline constexpr uint32_t virtualSize = 0xffff8116;
inline bool step(std::string value) {
  trace.push_back(std::move(value));
  return ++steps != failAt;
}
}  // namespace protection_test
class HalFile : public Print {
  FILE* file = nullptr;
  std::string path;
  bool installed = false;
  bool virtualFile = false;
  uint32_t virtualOffset = 0;

 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept
      : file(std::exchange(other.file, nullptr)),
        path(std::move(other.path)),
        installed(other.installed),
        virtualFile(other.virtualFile),
        virtualOffset(other.virtualOffset) {}
  HalFile& operator=(HalFile&& other) noexcept {
    close();
    file = std::exchange(other.file, nullptr);
    path = std::move(other.path);
    installed = other.installed;
    virtualFile = other.virtualFile;
    virtualOffset = other.virtualOffset;
    return *this;
  }
  ~HalFile() { close(); }
  bool open(const std::string& name, const char* mode) {
    close();
    installed = true;
    path = name;
    virtualFile = name.ends_with("offset-overflow.epub");
    virtualOffset = 0;
    if (!protection_test::step(std::string("open ") + mode + " " + path)) return false;
    file = std::fopen(path.c_str(), mode);
    return file;
  }
  bool close() {
    if (!file) return false;
    protection_test::trace.push_back("close " + path);
    const bool ok = std::fclose(file) == 0;
    file = nullptr;
    return ok;
  }
  bool isOpen() const { return file; }
  explicit operator bool() const { return file; }
  int32_t read(void* bytes, size_t length) {
    assert(installed);
    if (!file) return -1;
    if (!protection_test::step("read " + path + " " + std::to_string(position()) + " " + std::to_string(length))) {
      if (!protection_test::shortRead) return -1;
      if (length) --length;
    }
    if (virtualFile) {
      assert(length <= 1024);
      length = std::min<size_t>(length, protection_test::virtualSize - virtualOffset);
      memset(bytes, 0, length);
      for (const auto& [offset, data] : protection_test::segments) {
        const uint64_t start = std::max<uint64_t>(offset, virtualOffset);
        const uint64_t end = std::min<uint64_t>(uint64_t(offset) + data.size(), uint64_t(virtualOffset) + length);
        if (start < end)
          memcpy(static_cast<uint8_t*>(bytes) + start - virtualOffset, data.data() + start - offset, end - start);
      }
      virtualOffset += length;
      return length;
    }
    return std::fread(bytes, 1, length, file);
  }
  bool seek64(uint64_t offset) {
    assert(installed);
    if (!file || !protection_test::step("seek " + path + " " + std::to_string(offset))) return false;
    if (virtualFile) {
      assert(offset <= protection_test::virtualSize);
      virtualOffset = offset;
      return true;
    }
    return std::fseek(file, offset, SEEK_SET) == 0;
  }
  bool seek(uint32_t offset) { return seek64(offset); }
  size_t position() const {
    assert(installed);
    return virtualFile ? virtualOffset : file ? std::ftell(file) : 0;
  }
  uint64_t fileSize64() const {
    assert(installed);
    if (!file) return 0;
    if (virtualFile) return protection_test::virtualSize;
    auto offset = std::ftell(file);
    std::fseek(file, 0, SEEK_END);
    auto size = std::ftell(file);
    std::fseek(file, offset, SEEK_SET);
    return size;
  }
  size_t size() const { return fileSize64(); }
  int available() const { return size() - position(); }
  size_t write(const uint8_t* data, size_t length) override {
    assert(installed);
    return file ? std::fwrite(data, 1, length, file) : 0;
  }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
};
struct HalStorage {
  HalFile open(const char* name, int flags) {
    HalFile f;
    f.open(name, flags == O_RDONLY ? "rb" : "wb");
    return f;
  }
  bool openFileForRead(const char*, const std::string& name, HalFile& f) { return f.open(name, "rb"); }
  bool openFileForWrite(const char*, const std::string& name, HalFile& f) { return f.open(name, "wb"); }
  bool replaceFile(const char* from, const char* to) { return std::rename(from, to) == 0; }
};
inline HalStorage Storage;
inline uint32_t millis() { return 0; }
inline void delay(uint32_t) {}
