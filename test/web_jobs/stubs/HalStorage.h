#pragma once
#include "Arduino.h"
class HalFile {
  std::string path;
  size_t pos = 0;
  bool opened = false;
  size_t child = 0;

 public:
  HalFile() = default;
  HalFile(HalFile&& other) : path(std::move(other.path)), pos(other.pos), opened(other.opened), child(other.child) {
    other.opened = false;
  }
  HalFile& operator=(HalFile&& other) {
    if (opened) close();
    path = std::move(other.path);
    pos = other.pos;
    opened = other.opened;
    child = other.child;
    other.opened = false;
    return *this;
  }
  ~HalFile() {
    if (opened) close();
  }
  explicit operator bool() const { return opened; }
  bool isOpen() const { return opened; }
  void close() {
    oracle::record("close %s", path.c_str());
    opened = false;
  }
  size_t fileSize() const { return oracle::files[path].size(); }
  int read(uint8_t* p, size_t n) {
    size_t got = std::min(n, fileSize() - pos);
    if (oracle::fault == "read" && ++oracle::readCount == 2) got = 0;
    memcpy(p, oracle::files[path].data() + pos, got);
    pos += got;
    oracle::record("read %zu %zu %u", n, got, oracle::hash(p, got));
    return int(got);
  }
  size_t write(const uint8_t* p, size_t n) {
    size_t got = oracle::fault == "write" ? n / 2 : n;
    oracle::files[path].insert(oracle::files[path].end(), p, p + got);
    oracle::record("write %zu %zu %u", n, got, oracle::hash(p, got));
    return got;
  }
  size_t size() const { return fileSize(); }
  bool isDirectory() const { return oracle::dirs.count(path); }
  int available() const { return int(fileSize() - pos); }
  void getName(char* out, size_t n) {
    auto name = path.substr(path.rfind('/') + 1);
    snprintf(out, n, "%s", name.c_str());
  }
  bool rename(const char* p) {
    oracle::record("rename %s %s", path.c_str(), p);
    if (oracle::fault == "rename") return false;
    oracle::files[p] = oracle::files[path];
    oracle::files.erase(path);
    return true;
  }
  HalFile openNextFile() {
    std::vector<std::string> children;
    auto prefix = path == "/" ? path : path + "/";
    for (const auto& [p, b] : oracle::files)
      if (p.rfind(prefix, 0) == 0 && p.find('/', prefix.size()) == std::string::npos) children.push_back(p);
    HalFile f;
    if (child < children.size()) f.open(children[child++].c_str(), false);
    return f;
  }
  bool open(const char* p, bool write) {
    path = p;
    pos = 0;
    opened = oracle::fault != "open" && (write || oracle::files.count(p) || oracle::dirs.count(p));
    oracle::record("open %s %d %d", p, write, opened);
    if (opened && write) oracle::files[p].clear();
    return opened;
  }
};
struct StorageType {
  bool exists(const char* p) { return oracle::files.count(p) || oracle::dirs.count(p); }
  HalFile open(const char* p) {
    HalFile f;
    f.open(p, false);
    return f;
  }
  bool mkdir(const char* p) {
    oracle::record("mkdir %s", p);
    return oracle::dirs.insert(p).second;
  }
  bool rmdir(const char* p) {
    oracle::record("rmdir %s", p);
    return oracle::dirs.erase(p);
  }
  bool readFileToStream(const char* p, NetworkClient&) {
    oracle::record("stream %s", p);
    return oracle::fault != "stream";
  }
  bool openFileForWrite(const char* origin, const String& p, HalFile& f) {
    return openFileForWrite(origin, p.c_str(), f);
  }

  bool openFileForRead(const char*, const char* p, HalFile& f) { return f.open(p, false); }
  bool openFileForWrite(const char*, const char* p, HalFile& f) { return f.open(p, true); }
  bool remove(const char* p) {
    oracle::record("remove %s", p);
    return oracle::files.erase(p);
  }
  bool replaceFile(const char* a, const char* b) {
    oracle::record("replace %s %s", a, b);
    if (oracle::fault == "replace") return false;
    oracle::files[b] = oracle::files[a];
    oracle::files.erase(a);
    return true;
  }
};
inline StorageType Storage;
