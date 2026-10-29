#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace booktest {
inline std::map<std::string, std::vector<uint8_t>> files;
inline size_t calls = 0, fail = 0;
inline std::string displayPath(std::string path) {
  const auto begin = path.find("/xtc_");
  if (begin != std::string::npos) {
    const auto end = path.find('/', begin + 1);
    path.replace(begin, end == std::string::npos ? path.size() - begin : end - begin, "/xtc_key");
  }
  return path;
}
inline bool step(const char* op, const std::string& path, uint64_t arg = 0) {
  const bool failed = ++calls == fail;
  std::printf("io %s %s %llu %d\n", op, displayPath(path).c_str(), static_cast<unsigned long long>(arg), failed);
  return !failed;
}
}
class HalFile {
 public:
  ~HalFile() { if (opened) close(); }
  bool seek64(uint64_t offset) {
    if (!booktest::step("seek", path, offset) || !opened || offset > booktest::files[path].size()) return false;
    position = offset;
    return true;
  }
  bool seek(uint32_t offset) { return seek64(offset); }
  int read(void* out, size_t size) {
    if (!booktest::step("read", path, size) || !opened) return -1;
    size = std::min(size, booktest::files[path].size() - position);
    if (size) std::memcpy(out, booktest::files[path].data() + position, size);
    position += size;
    return static_cast<int>(size);
  }
  size_t write(const void* data, size_t size) {
    if (!booktest::step("write", path, size) || !opened) return 0;
    auto& bytes = booktest::files[path];
    bytes.resize(std::max(bytes.size(), position + size));
    if (size) std::memcpy(bytes.data() + position, data, size);
    position += size;
    return size;
  }
  uint64_t fileSize64() const { return opened ? booktest::files[path].size() : 0; }
  size_t size() const { return fileSize64(); }
  bool available() const { return opened && position < booktest::files[path].size(); }
  bool isOpen() const { return opened; }
  explicit operator bool() const { return opened; }
  void close() { booktest::step("close", path); opened = false; }
  bool open(const std::string& name, bool writing) {
    path = name;
    position = 0;
    opened = booktest::step(writing ? "open-write" : "open-read", path) &&
             (writing || booktest::files.count(path));
    if (opened && writing) booktest::files[path].clear();
    return opened;
  }
 private:
  std::string path;
  size_t position = 0;
  bool opened = false;
};
struct HalStorage {
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path, false); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path, true); }
  bool exists(const char* path) { return booktest::step("exists", path) && booktest::files.count(path); }
  bool mkdir(const char* path) { return booktest::step("mkdir", path); }
  bool removeDir(const char* path) { return booktest::step("rmdir", path); }
};
inline HalStorage Storage;
