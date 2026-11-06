#pragma once
// The book facade composition links a different HAL fake in another TU.
// Give this in-memory volume distinct symbols to avoid an ODR collision.
#define HalStorage ClippingTestStorage
#define HalFile ClippingTestFile
#include "../library_builder/stubs/HalStorage.h"
#undef HalFile
#undef HalStorage
#undef Storage
#include <xp_clippings.h>
#include <xp_epub_session.h>

#include <cassert>
#include <functional>
#include <set>
#include <string>

namespace clipping_fixture {
// Physical volume for real NativeStore tests; the store owns every record.
struct StorageAdapter {
  inline static StorageAdapter* active = nullptr;
  XpEpubLeaves previous;
  XpAppResources resources;
  std::set<void*> files;
  void (*io)() = nullptr;
  void (*writing)(const char*) = nullptr;
  bool quietLog = false;
  static std::string text(XpAppText value) { return {reinterpret_cast<const char*>(value.data), value.len}; }
  static bool path(XpAppText value) { return text(value).rfind("/.crosspoint/clippings", 0) == 0; }
  static void check() {
    assert(active);
    if (active->io) active->io();
  }
  static ClippingTestFile& file(void* raw) {
    assert(active->files.count(raw));
    return *static_cast<ClippingTestFile*>(raw);
  }
  static void* retain(ClippingTestFile value) {
    if (!value) return nullptr;
    auto* result = new ClippingTestFile(std::move(value));
    active->files.insert(result);
    return result;
  }
  inline static const XpClippingPhysical physical{
      [](XpAppText name, XpClippingOpenMode mode) -> void* {
        check();
        const int flags[] = {0, O_WRONLY | O_CREAT | O_TRUNC, O_WRONLY | O_CREAT | O_APPEND, O_RDWR};
        assert(unsigned(mode) < 4);
        return retain(ClippingTestStorage::getInstance().open(text(name).c_str(), flags[mode]));
      },
      [](void* raw, uint32_t offset) -> uint8_t {
        check();
        return file(raw).seekCur(offset);
      },
      [](void* raw) -> int32_t {
        check();
        return file(raw).available();
      },
      [](void* raw, uint32_t size) -> uint8_t {
        check();
        return file(raw).truncate(size);
      },
      [](void* raw) -> uint8_t {
        check();
        return file(raw).isDirectory();
      },
      [](void* raw, uint8_t* failed) -> void* {
        check();
        *failed = 0;
        return retain(file(raw).openNextFile());
      },
      [](void* raw, uint8_t* output, uint32_t capacity) -> uint32_t {
        check();
        return file(raw).getName(reinterpret_cast<char*>(output), capacity);
      },
      []() -> int64_t { return 1700000000; },
      [](uint8_t* output, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) output[i] = uint8_t(i + 1);
      },
  };

  StorageAdapter(XpEpubLeaves& leaves, XpAppResources& original) : previous(leaves), resources(original) {
    assert(!active);
    active = this;
    leaves.clipping_physical = &physical;
    leaves.file_open_read = [](XpAppText name, XpAppText origin) -> void* {
      if (!path(name)) return active->previous.file_open_read(name, origin);
      check();
      ClippingTestFile value;
      return ClippingTestStorage::getInstance().openFileForRead("CLIP", text(name), value) ? retain(std::move(value))
                                                                                           : nullptr;
    };
    leaves.file_read = [](void* raw, uint8_t* output, uint32_t count) -> int32_t {
      if (!active->files.count(raw)) return active->previous.file_read(raw, output, count);
      check();
      return file(raw).read(output, count);
    };
    leaves.file_write = [](void* raw, const uint8_t* input, uint32_t count) -> uint32_t {
      if (!active->files.count(raw)) return active->previous.file_write(raw, input, count);
      check();
      if (active->writing) active->writing(file(raw).path.c_str());
      return file(raw).write(input, count);
    };
    leaves.file_seek = [](void* raw, uint32_t offset) -> uint8_t {
      if (!active->files.count(raw)) return active->previous.file_seek(raw, offset);
      check();
      return file(raw).seek(offset);
    };
    leaves.file_position = [](void* raw) -> uint32_t {
      if (!active->files.count(raw)) return active->previous.file_position(raw);
      check();
      return file(raw).position();
    };
    leaves.file_size = [](void* raw) -> uint32_t {
      if (!active->files.count(raw)) return active->previous.file_size(raw);
      check();
      return file(raw).size();
    };
    leaves.file_flush = [](void* raw) {
      if (!active->files.count(raw)) return active->previous.file_flush(raw);
      check();
      file(raw).flush();
    };
    leaves.file_close = [](void* raw) -> uint8_t {
      if (!active->files.count(raw)) return active->previous.file_close(raw);
      check();
      file(raw).close();
      active->files.erase(raw);
      delete static_cast<ClippingTestFile*>(raw);
      return 1;
    };
    original.path_exists = [](XpAppText name) -> uint8_t {
      if (!path(name)) return active->resources.path_exists(name);
      check();
      return ClippingTestStorage::getInstance().exists(text(name).c_str());
    };
    leaves.path_mkdir = [](XpAppText name) -> uint8_t {
      if (!path(name) && text(name) != "/.crosspoint") return active->previous.path_mkdir(name);
      check();
      return ClippingTestStorage::getInstance().mkdir(text(name).c_str());
    };
    leaves.path_remove = [](XpAppText name) -> uint8_t {
      if (!path(name)) return active->previous.path_remove(name);
      check();
      return ClippingTestStorage::getInstance().remove(text(name).c_str());
    };
    leaves.path_rename = [](XpAppText from, XpAppText to) -> uint8_t {
      if (!path(from) && !path(to)) return active->previous.path_rename(from, to);
      check();
      return ClippingTestStorage::getInstance().rename(text(from).c_str(), text(to).c_str());
    };
    leaves.native_path_hash = [](XpAppText name) -> uint64_t { return std::hash<std::string>{}(text(name)); };
    leaves.log = [](uint8_t level, XpAppText origin, XpAppText message) {
      if (!active->quietLog) active->previous.log(level, origin, message);
    };
  }
  ~StorageAdapter() {
    assert(active == this && files.empty());
    active = nullptr;
  }
};
}  // namespace clipping_fixture
#undef Storage
