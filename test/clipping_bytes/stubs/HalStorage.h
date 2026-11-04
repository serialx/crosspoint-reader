#pragma once

#include <common/FsApiConstants.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Host model of the physical SD card. It records every physical operation in
// order, injects a failure or a short transfer at a chosen attempt, and keeps
// the bytes each operation leaves behind. The SDCardManager method bodies are
// extracted verbatim from the pinned SDK by run.py; only this volume layer is
// substituted for SdFat.
namespace clip_test {

struct Node {
  bool directory = false;
  std::vector<uint8_t> bytes;
};

inline std::map<std::string, std::shared_ptr<Node>> nodes;
inline std::vector<std::string> io;
inline std::map<std::string, unsigned> attempts;
inline std::map<std::string, std::vector<unsigned>> failures;
inline std::map<std::string, std::pair<unsigned, size_t>> shortOps;
inline std::vector<std::string> paths;
inline std::map<std::string, size_t> pathIndex;
inline bool recordIo = true;

inline std::string hex(const uint8_t* data, const size_t size) {
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    result += digits[data[i] >> 4];
    result += digits[data[i] & 15];
  }
  return result;
}

inline std::string hex(const std::string& text) {
  return hex(reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

// Stable short name for a path in the recorded I/O, so the golden keeps one
// readable path table per case instead of repeating long paths per operation.
inline std::string pathRef(const std::string& path) {
  const auto found = pathIndex.find(path);
  if (found != pathIndex.end()) return "#" + std::to_string(found->second);
  const size_t index = paths.size();
  paths.push_back(path);
  pathIndex[path] = index;
  return "#" + std::to_string(index);
}

// Counts the attempt for `op` and reports whether a failure was requested.
inline bool injected(const std::string& op) {
  const unsigned attempt = ++attempts[op];
  const auto found = failures.find(op);
  return found != failures.end() &&
         std::find(found->second.begin(), found->second.end(), attempt) != found->second.end();
}

// Short-transfer limit for the attempt `event()` has just counted.
inline size_t shortLimit(const std::string& op, const size_t requested) {
  const auto found = shortOps.find(op);
  if (found == shortOps.end() || found->second.first != attempts[op]) return requested;
  return std::min(requested, found->second.second);
}

inline bool event(const std::string& op, const std::string& detail) {
  const bool ok = !injected(op);
  if (recordIo) io.push_back(op + ":" + detail + ":" + (ok ? "ok" : "fail"));
  return ok;
}

inline void note(const std::string& text) {
  if (recordIo) io.push_back(text);
}

inline std::string accessMode(const oflag_t oflag) {
  if (oflag & (O_APPEND | O_AT_END)) return "append";
  if (oflag & O_TRUNC) return "truncate";
  if (oflag & O_RDWR) return "read-write";
  if (oflag == O_RDONLY) return "read-only";
  return "write";
}

inline bool isDirectory(const std::string& path) {
  const auto found = nodes.find(path);
  return found != nodes.end() && found->second->directory;
}

inline std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return "";
  return slash == 0 ? "/" : path.substr(0, slash);
}

inline std::vector<std::string> childrenOf(const std::string& path) {
  // FAT lists the dot entries first; production skips them by name.
  std::vector<std::string> children{".", ".."};
  for (const auto& [name, node] : nodes) {
    if (name != path && parentOf(name) == path) children.push_back(name);
  }
  return children;
}

struct Handle {
  std::shared_ptr<Node> node;
  std::string path;
  std::vector<std::string> children;
  size_t pos = 0;
  size_t child = 0;
  bool open = true;

  void release() {
    if (!open) return;
    open = false;
    clip_test::event("close", clip_test::pathRef(path));
  }

  ~Handle() { release(); }
};

inline void reset() {
  nodes.clear();
  io.clear();
  paths.clear();
  pathIndex.clear();
  attempts.clear();
  failures.clear();
  shortOps.clear();
  recordIo = true;
  auto root = std::make_shared<Node>();
  root->directory = true;
  nodes["/"] = root;
}

inline void addFile(const std::string& path, const std::string& bytes) {
  auto node = std::make_shared<Node>();
  node->bytes.assign(bytes.begin(), bytes.end());
  nodes[path] = node;
  std::string parent = parentOf(path);
  while (!parent.empty() && !nodes.count(parent)) {
    auto directory = std::make_shared<Node>();
    directory->directory = true;
    nodes[parent] = directory;
    parent = parentOf(parent);
  }
}

inline void addDirectory(const std::string& path) {
  std::string current = path;
  while (!current.empty() && !nodes.count(current)) {
    auto node = std::make_shared<Node>();
    node->directory = true;
    nodes[current] = node;
    current = parentOf(current);
  }
}

}  // namespace clip_test

class HalFile {
  std::unique_ptr<clip_test::Handle> impl_;

 public:
  HalFile() = default;
  explicit HalFile(std::unique_ptr<clip_test::Handle> impl) : impl_(std::move(impl)) {}
  HalFile(HalFile&&) = default;
  HalFile& operator=(HalFile&&) = default;
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool isOpen() const { return impl_ && impl_->open; }
  explicit operator bool() const { return isOpen(); }
  bool isDirectory() const { return impl_ && impl_->node && impl_->node->directory; }

  bool close() {
    if (!impl_) return false;
    if (!impl_->open) return true;
    impl_->open = false;
    return clip_test::event("close", clip_test::pathRef(impl_->path));
  }

  void flush() {
    if (impl_ && impl_->open) clip_test::event("flush", clip_test::pathRef(impl_->path));
  }

  size_t getName(char* name, size_t len) {
    if (!impl_) return 0;
    const size_t slash = impl_->path.find_last_of('/');
    const std::string base = slash == std::string::npos ? impl_->path : impl_->path.substr(slash + 1);
    if (len > 0) {
      std::strncpy(name, base.c_str(), len);
      name[len - 1] = '\0';
    }
    return base.size();
  }

  HalFile openNextFile() {
    if (!impl_ || !impl_->node || !impl_->node->directory || impl_->child >= impl_->children.size()) return {};
    const std::string entry = impl_->children[impl_->child++];
    clip_test::note("next:" + clip_test::pathRef(impl_->path) + ":" + clip_test::pathRef(entry));
    auto handle = std::make_unique<clip_test::Handle>();
    handle->path = entry;
    if (entry == ".") {
      handle->node = impl_->node;
    } else if (entry == "..") {
      const std::string parent = clip_test::parentOf(impl_->path);
      handle->node = parent.empty() ? impl_->node : clip_test::nodes[parent];
    } else {
      handle->node = clip_test::nodes[entry];
    }
    if (handle->node && handle->node->directory) handle->children = clip_test::childrenOf(entry);
    return HalFile(std::move(handle));
  }

  void rewindDirectory() {
    if (impl_) impl_->child = 0;
  }

  size_t fileSize() {
    if (!impl_ || !impl_->node) return 0;
    return impl_->node->bytes.size();
  }

  size_t size() { return fileSize(); }

  uint64_t fileSize64() { return fileSize(); }

  size_t position() const { return impl_ ? impl_->pos : 0; }

  int available() const {
    if (!impl_ || !impl_->node) return -1;
    return static_cast<int>(impl_->node->bytes.size() - std::min(impl_->pos, impl_->node->bytes.size()));
  }

  bool seekSet(const size_t offset) {
    if (!impl_ || !impl_->open || !impl_->node) return false;
    if (!clip_test::event("seek", clip_test::pathRef(impl_->path) + "@" + std::to_string(offset))) return false;
    if (offset > impl_->node->bytes.size()) return false;
    impl_->pos = offset;
    return true;
  }

  bool seek(const size_t offset) { return seekSet(offset); }

  bool seekCur(const int64_t offset) {
    const int64_t target = static_cast<int64_t>(impl_ ? impl_->pos : 0) + offset;
    if (target < 0) return false;
    return seekSet(static_cast<size_t>(target));
  }

  bool truncate(const uint64_t length) {
    if (!impl_ || !impl_->open || !impl_->node) return false;
    if (!clip_test::event("truncate", clip_test::pathRef(impl_->path) + ":" + std::to_string(length))) return false;
    if (length > impl_->node->bytes.size()) return false;
    impl_->node->bytes.resize(length);
    impl_->pos = std::min(impl_->pos, static_cast<size_t>(length));
    return true;
  }

  int read(void* out, const size_t size) {
    if (!impl_ || !impl_->open || !impl_->node || impl_->node->directory) {
      clip_test::event("read", "closed");
      return -1;
    }
    if (size == 0) return 0;
    if (!clip_test::event(
            "read", clip_test::pathRef(impl_->path) + "@" + std::to_string(impl_->pos) + "+" + std::to_string(size))) {
      return -1;
    }
    const size_t remaining = impl_->node->bytes.size() - std::min(impl_->pos, impl_->node->bytes.size());
    const size_t limit = clip_test::shortLimit("read", size);
    const size_t count = std::min({size, remaining, limit});
    if (limit < size) clip_test::note("short-read:" + std::to_string(size) + ":" + std::to_string(count));
    if (count > 0) std::memcpy(out, impl_->node->bytes.data() + impl_->pos, count);
    impl_->pos += count;
    return static_cast<int>(count);
  }

  size_t write(const uint8_t* data, const size_t size) {
    if (!impl_ || !impl_->open || !impl_->node || impl_->node->directory) {
      clip_test::event("write", "closed");
      return 0;
    }
    if (size == 0) return 0;
    if (!clip_test::event(
            "write", clip_test::pathRef(impl_->path) + "@" + std::to_string(impl_->pos) + "+" + std::to_string(size))) {
      return 0;
    }
    const size_t count = clip_test::shortLimit("write", size);
    if (count != size) clip_test::note("short-write:" + std::to_string(size) + ":" + std::to_string(count));
    impl_->node->bytes.resize(std::max(impl_->node->bytes.size(), impl_->pos + count));
    if (count > 0) std::memcpy(impl_->node->bytes.data() + impl_->pos, data, count);
    impl_->pos += count;
    return count;
  }

  size_t write(const void* data, const size_t size) { return write(static_cast<const uint8_t*>(data), size); }
};

using FsFile = HalFile;

struct PhysicalVolume {
  HalFile open(const char* path, const oflag_t oflag);
  bool mkdir(const char* path, bool pFlag);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* from, const char* to);
};

// Method bodies in SdkInline.inc are extracted verbatim from the pinned SDK.
class SDCardManager {
  PhysicalVolume volume;

  PhysicalVolume& vol() { return volume; }

 public:
  bool openFileForRead(const char* moduleName, const char* path, FsFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
    return openFileForRead(moduleName, path.c_str(), file);
  }
#include "SdkInline.inc"
};

inline SDCardManager Storage;

inline HalFile PhysicalVolume::open(const char* path, const oflag_t oflag) {
  using namespace clip_test;
  const std::string target = path;
  const bool create = (oflag & O_CREAT) != 0;
  const bool truncate = (oflag & O_TRUNC) != 0;
  const bool append = (oflag & (O_APPEND | O_AT_END)) != 0;
  if (!event("open", pathRef(target) + ":" + accessMode(oflag))) return {};
  auto found = nodes.find(target);
  if (found == nodes.end()) {
    // FAT needs a creatable parent and an explicit create flag.
    if (!create || !isDirectory(parentOf(target))) return {};
    nodes[target] = std::make_shared<Node>();
    found = nodes.find(target);
  }
  if (found->second->directory && (create || truncate || append)) return {};
  auto handle = std::make_unique<Handle>();
  handle->node = found->second;
  handle->path = target;
  if (found->second->directory) {
    handle->children = childrenOf(target);
  } else {
    if (truncate) found->second->bytes.clear();
    if (append) handle->pos = found->second->bytes.size();
  }
  return HalFile(std::move(handle));
}

inline bool PhysicalVolume::mkdir(const char* path, const bool pFlag) {
  using namespace clip_test;
  const std::string target = path;
  if (!event("mkdir", pathRef(target))) return false;
  std::vector<std::string> missing;
  for (std::string current = target; !current.empty() && !isDirectory(current);) {
    if (nodes.count(current)) return false;
    missing.push_back(current);
    current = parentOf(current);
    if (current.empty() && target != "/") return false;
  }
  if (!pFlag && missing.size() > 1) return false;
  for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
    const auto& directory = *it;
    auto node = std::make_shared<Node>();
    node->directory = true;
    nodes[directory] = node;
  }
  return true;
}

inline bool PhysicalVolume::exists(const char* path) {
  using namespace clip_test;
  const bool found = nodes.count(path) != 0;
  const bool ok = event("exists", pathRef(path)) && found;
  note(std::string("exists-result:") + (ok ? "true" : "false"));
  return ok;
}

inline bool PhysicalVolume::remove(const char* path) {
  using namespace clip_test;
  const std::string target = path;
  if (!event("remove", pathRef(target))) return false;
  const auto found = nodes.find(target);
  if (found == nodes.end() || found->second->directory) return false;
  nodes.erase(found);
  return true;
}

inline bool PhysicalVolume::rename(const char* from, const char* to) {
  using namespace clip_test;
  const std::string source = from;
  const std::string target = to;
  if (!event("rename", pathRef(source) + ":" + pathRef(target))) return false;
  if (!nodes.count(source) || nodes.count(target)) return false;
  std::vector<std::pair<std::string, std::shared_ptr<Node>>> moved;
  const std::string prefix = source + "/";
  for (auto it = nodes.begin(); it != nodes.end();) {
    if (it->first == source || it->first.rfind(prefix, 0) == 0) {
      moved.emplace_back(target + it->first.substr(source.size()), it->second);
      it = nodes.erase(it);
    } else {
      ++it;
    }
  }
  for (auto& entry : moved) nodes[entry.first] = entry.second;
  return true;
}
