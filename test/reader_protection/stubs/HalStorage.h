#pragma once
#include <Print.h>
#include <fcntl.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
namespace protection_test {
inline std::vector<std::string> trace;
inline size_t steps = 0, failAt = 0;
inline bool step(std::string value) {
  trace.push_back(std::move(value));
  return ++steps != failAt;
}
}  // namespace protection_test
class HalFile : public Print {
  FILE* file = nullptr;
  std::string path;
  bool installed = false;

 public:
  HalFile() = default;
  HalFile(const HalFile&) = delete;
  HalFile(HalFile&& other) noexcept
      : file(std::exchange(other.file, nullptr)), path(std::move(other.path)), installed(other.installed) {}
  HalFile& operator=(HalFile&& other) noexcept {
    close();
    file = std::exchange(other.file, nullptr);
    path = std::move(other.path);
    installed = other.installed;
    return *this;
  }
  ~HalFile() { close(); }
  bool open(const std::string& name, const char* mode) {
    close();
    installed = true;
    path = name;
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
    if (!protection_test::step("read " + path + " " + std::to_string(position()) + " " + std::to_string(length)))
      return -1;
    return std::fread(bytes, 1, length, file);
  }
  bool seek64(uint64_t offset) {
    assert(installed);
    return file && protection_test::step("seek " + path + " " + std::to_string(offset)) &&
           std::fseek(file, offset, SEEK_SET) == 0;
  }
  bool seek(uint32_t offset) { return seek64(offset); }
  size_t position() const {
    assert(installed);
    return file ? std::ftell(file) : 0;
  }
  uint64_t fileSize64() const {
    assert(installed);
    if (!file) return 0;
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
