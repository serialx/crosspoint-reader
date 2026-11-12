#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../hal_fake_strict.h"

constexpr int O_WRITE = 1, O_CREAT = 2, O_TRUNC = 4;
struct Node {
  std::vector<uint8_t> bytes;
  bool directory = false;
};
inline std::map<std::string, Node> files;
inline int writeLimit = -1, readLimit = -1;
inline std::string failOpen, failSeek;
class HalFile {
 public:
  Node* node = nullptr;
  std::string path;
  size_t cursor = 0, next = 0;
  hal_fake::Impl initialized;  // A move leaves the source without an impl.
  HalFile() = default;
  HalFile(Node* n, std::string p) : node(n), path(std::move(p)), initialized(true) {}
  explicit operator bool() const { return initialized && node != nullptr; }
  bool close() {
    hal_fake::requireImpl(initialized, "close", path.c_str());
    node = nullptr;
    return true;
  }
  size_t position() const {
    hal_fake::requireImpl(initialized, "position", path.c_str());
    return cursor;
  }
  size_t fileSize() const {
    hal_fake::requireImpl(initialized, "fileSize", path.c_str());
    return node ? node->bytes.size() : 0;
  }
  bool seekSet(size_t n) {
    hal_fake::requireImpl(initialized, "seekSet", path.c_str());
    if (!node || path == failSeek || n > fileSize()) return false;
    cursor = n;
    return true;
  }
  int read(void* data, size_t n) {
    hal_fake::requireImpl(initialized, "read", path.c_str());
    if (!node || readLimit == 0) return -1;
    n = std::min(n, fileSize() - cursor);
    if (readLimit > 0) {
      n = std::min(n, size_t(readLimit));
      readLimit -= n;
    }
    if (n) memcpy(data, node->bytes.data() + cursor, n);
    cursor += n;
    return int(n);
  }
  int read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
  }
  size_t write(const void* data, size_t n) {
    hal_fake::requireImpl(initialized, "write", path.c_str());
    if (!node || writeLimit == 0) return 0;
    if (writeLimit > 0) {
      n = std::min(n, size_t(writeLimit));
      writeLimit -= n;
    }
    node->bytes.resize(std::max(node->bytes.size(), cursor + n));
    if (n) memcpy(node->bytes.data() + cursor, data, n);
    cursor += n;
    return n;
  }
  bool isDirectory() const {
    hal_fake::requireImpl(initialized, "isDirectory", path.c_str());
    return node && node->directory;
  }
  void rewindDirectory() {
    hal_fake::requireImpl(initialized, "rewindDirectory", path.c_str());
    next = 0;
  }
  size_t getName(char* out, size_t cap) {
    hal_fake::requireImpl(initialized, "getName", path.c_str());
    auto name = path.substr(path.rfind('/') + 1);
    if (!cap) return 0;
    size_t n = std::min(cap - 1, name.size());
    memcpy(out, name.data(), n);
    out[n] = 0;
    return n;
  }
  HalFile openNextFile() {
    hal_fake::requireImpl(initialized, "openNextFile", path.c_str());
    size_t i = 0;
    for (auto& [name, value] : files) {
      if (name.rfind(path + '/', 0) != 0 || name.find('/', path.size() + 1) != std::string::npos) continue;
      if (i++ == next) {
        ++next;
        return {&value, name};
      }
    }
    return {nullptr, ""};
  }
};
struct HalStorage {
  HalFile open(const char* path, int flags = 0) {
    if (failOpen == path) return {nullptr, path};
    auto it = files.find(path);
    if (flags & O_CREAT) it = files.try_emplace(path).first;
    if (it == files.end()) return {nullptr, path};
    if (flags & O_TRUNC) it->second.bytes.clear();
    return {&it->second, path};
  }
  bool exists(const char* path) { return files.contains(path); }
  bool remove(const char* path) { return files.erase(path); }
  bool openFileForRead(const char*, const char* path, HalFile& out) {
    out = open(path);
    return bool(out);
  }
  bool openFileForRead(const char* tag, const std::string& path, HalFile& out) {
    return openFileForRead(tag, path.c_str(), out);
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& out) {
    out = open(path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
    return bool(out);
  }
};
inline HalStorage Storage;
