#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

struct StorageTrace {
  std::map<std::string, std::vector<uint8_t>> files;
  bool openOk = true;
  bool renameOk = true;
  size_t writeLimit = 100;
  bool trackLifetime = false;
  int liveFiles = 0;
};
inline StorageTrace storageTrace;

struct HalFile {
  std::string path;
  bool tracked = false;
  ~HalFile() {
    close();
    if (tracked) std::printf("file-release %d\n", --storageTrace.liveFiles);
  }
  void close() {
    if (!path.empty()) std::printf("close %s\n", path.c_str());
    path.clear();
  }
  size_t write(const uint8_t* bytes, size_t len) {
    const size_t written = std::min(len, storageTrace.writeLimit);
    storageTrace.files[path].assign(bytes, bytes + written);
    std::printf("write %zu", written);
    for (size_t i = 0; i < written; ++i) std::printf(" %02x", bytes[i]);
    std::puts("");
    return written;
  }
  int read(uint8_t* bytes, size_t len) {
    const auto& data = storageTrace.files[path];
    const size_t count = std::min(len, data.size());
    std::copy_n(data.begin(), count, bytes);
    return static_cast<int>(count);
  }
  void flush() { std::puts("flush"); }
};

struct StorageStub {
  bool openFileForWrite(const char*, const std::string& path, HalFile& f) {
    std::printf("open-write %s %d\n", path.c_str(), storageTrace.openOk);
    if (!storageTrace.openOk) return false;
    f.path = path;
    f.tracked = storageTrace.trackLifetime;
    if (f.tracked) std::printf("file-allocate %d\n", ++storageTrace.liveFiles);
    return true;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& f) {
    const bool ok = storageTrace.openOk && filesContains(path);
    std::printf("open-read %s %d\n", path.c_str(), ok);
    if (!ok) return false;
    f.path = path;
    f.tracked = storageTrace.trackLifetime;
    if (f.tracked) std::printf("file-allocate %d\n", ++storageTrace.liveFiles);
    return true;
  }
  bool filesContains(const std::string& path) const { return storageTrace.files.count(path) != 0; }
  bool exists(const char* path) const { return filesContains(path); }
  bool remove(const char* path) {
    std::printf("remove %s\n", path);
    if (storageTrace.trackLifetime) std::printf("live-files %d\n", storageTrace.liveFiles);
    return storageTrace.files.erase(path) != 0;
  }
  bool rename(const char* from, const char* to) {
    std::printf("rename %s %s %d\n", from, to, storageTrace.renameOk);
    if (!storageTrace.renameOk) return false;
    storageTrace.files[to] = storageTrace.files[from];
    storageTrace.files.erase(from);
    return true;
  }
};
inline StorageStub Storage;
