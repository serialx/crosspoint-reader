#pragma once
#include <Arduino.h>

#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace bookmark_test {
std::string hex(const std::string& bytes);
inline std::map<std::string, std::string> files;
inline std::set<std::string> directories;
inline std::vector<std::string> io;
inline std::map<std::string, unsigned> calls;
inline std::string fail;
inline unsigned failAt = 1;
inline std::map<std::string, unsigned> failures;
inline unsigned lockDepth = 0, lockEpoch = 0;
inline size_t writeLimit = SIZE_MAX;
inline size_t readLimit = SIZE_MAX;
inline bool event(const std::string& op, const std::string& detail) {
  const unsigned attempt = ++calls[op];
  const bool ok = !(op == fail && attempt == failAt) && !(failures.count(op) && failures.at(op) == attempt);
  io.push_back(op + ":" + detail + ":" + (ok ? "ok" : "fail"));
  return ok;
}
}  // namespace bookmark_test

constexpr int O_RDONLY = 0;
constexpr int O_RDWR = 1;
constexpr int O_CREAT = 2;
constexpr int O_TRUNC = 4;

class FsFile {
  std::string path;
  size_t position = 0;
  bool opened = false;
  unsigned epoch = 0;

 public:
  FsFile() = default;
  FsFile(std::string p, bool ok) : path(std::move(p)), opened(ok), epoch(bookmark_test::lockEpoch) {}
  explicit operator bool() const { return opened; }
  bool available() const { return position < std::min(bookmark_test::files.at(path).size(), bookmark_test::readLimit); }
  int read() {
    assert(bookmark_test::lockDepth == 1 && epoch == bookmark_test::lockEpoch);
    using namespace bookmark_test;
    if (!event("read", std::to_string(position))) return -1;
    return static_cast<unsigned char>(files.at(path)[position++]);
  }
  size_t print(const String& text) { return write(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()); }
  size_t write(const uint8_t* bytes, size_t size) {
    using namespace bookmark_test;
    assert(lockDepth == 1 && epoch == lockEpoch);
    size_t count = event("write", std::to_string(size)) ? std::min<size_t>(size, writeLimit) : 0;
    files[path].append(reinterpret_cast<const char*>(bytes), count);
    io.push_back("written:" + std::to_string(count));
    return count;
  }
  bool close() {
    assert(bookmark_test::lockDepth == 1 && epoch == bookmark_test::lockEpoch);
    opened = false;
    return bookmark_test::event("close", bookmark_test::hex(path));
  }
};

struct PhysicalVolume {
  FsFile open(const char* path, int flags) {
    using namespace bookmark_test;
    const bool write = flags != O_RDONLY;
    bool ok = event(write ? "open-write" : "open-read", hex(path));
    if (write) {
      const std::string p(path);
      ok = ok && directories.count(p.substr(0, p.find_last_of('/')));
      if (ok) files[path].clear();
    } else {
      ok = ok && files.count(path);
    }
    io.push_back(std::string("opened:") + (ok ? "true" : "false"));
    return FsFile(path, ok);
  }
  bool mkdir(const char* path, bool) {
    using namespace bookmark_test;
    const bool ok = event("mkdir", hex(path));
    if (ok) {
      std::string p(path);
      while (p.size() > 1 && p.back() == '/') p.pop_back();
      directories.insert(p);
      while (p.find_last_of('/') > 0) {
        p.erase(p.find_last_of('/'));
        directories.insert(p);
      }
    }
    return ok;
  }
  bool exists(const char* path) {
    using namespace bookmark_test;
    const bool found = files.count(path) != 0;
    const bool ok = event("exists", hex(path)) && found;
    io.push_back(std::string("exists-result:") + (ok ? "true" : "false"));
    return ok;
  }
  bool remove(const char* path) {
    using namespace bookmark_test;
    const bool ok = event("remove", hex(path));
    return ok && files.erase(path) != 0;
  }
  bool rename(const char* from, const char* to) {
    using namespace bookmark_test;
    const bool ok = event("rename", hex(from) + ":" + hex(to)) && files.count(from) && !files.count(to);
    if (ok) {
      files[to] = files.at(from);
      files.erase(from);
    }
    return ok;
  }
};

// Method bodies are extracted verbatim from the pinned SDK by run.py.
class SDCardManager {
  PhysicalVolume volume;
  bool initialized = true;
  PhysicalVolume& vol() { return volume; }

 public:
  bool ready() const { return initialized; }
  String readFile(const char* path);
  bool writeFile(const char* path, const String& content);
  bool openFileForRead(const char* moduleName, const char* path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, FsFile& file);
#include "SdkInline.inc"
};
inline SDCardManager SDCard;
class HalStorage {
 public:
  struct StorageLock {
    StorageLock() {
      assert(bookmark_test::lockDepth++ == 0);
      ++bookmark_test::lockEpoch;
    }
    ~StorageLock() { assert(--bookmark_test::lockDepth == 0); }
  };
  String readFile(const char* path) {
    StorageLock lock;
    return SDCard.readFile(path);
  }
  bool writeFile(const char* path, const String& value) {
    StorageLock lock;
    return SDCard.writeFile(path, value);
  }
  bool writeFileAtomic(const char* path, const uint8_t* bytes, size_t size);
  bool exists(const char* path) {
    StorageLock lock;
    return SDCard.exists(path);
  }
  bool mkdir(const char* path) {
    StorageLock lock;
    return SDCard.mkdir(path);
  }
};
inline HalStorage Storage;
