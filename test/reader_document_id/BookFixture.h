#pragma once
#include <xp_epub_session.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// A generated plain EPUB and a usable v10 cache, independent of KODoc faults.
namespace book_fixture {
inline std::vector<uint8_t> zip{
#include "BookData.inc"
};
inline std::vector<uint8_t> cache = [] {
  std::vector<uint8_t> bytes{10};
  auto integer = [&](uint32_t value, size_t width) {
    while (width--) {
      bytes.push_back(uint8_t(value));
      value >>= 8;
    }
  };
  integer(29, 4);
  integer(1, 2);
  integer(0, 2);
  for (int i = 0; i < 5; ++i) integer(0, 4);
  integer(33, 4);
  const char name[] = "chapter.xhtml";
  integer(sizeof(name) - 1, 4);
  bytes.insert(bytes.end(), name, name + sizeof(name) - 1);
  integer(17, 4);
  integer(0xffff, 2);
  return bytes;
}();
struct File {
  const std::vector<uint8_t>* bytes;
  size_t offset = 0;
};
inline int opened = 0, closed = 0, cacheLocks = 0, liveFiles = 0;
inline void install(XpEpubLeaves& leaves, XpAppResources& resources) {
  leaves.cache_lock = []() {
    assert(cacheLocks == 0);
    ++cacheLocks;
  };
  leaves.cache_unlock = []() {
    assert(cacheLocks == 1);
    --cacheLocks;
  };
  leaves.heap_snapshot = [](XpEpubHeap* out) { *out = {200 * 1024, 200 * 1024}; };
  resources.buffer_allocate = [](XpAppMemoryDomain, size_t size) { return static_cast<uint8_t*>(std::malloc(size)); };
  resources.buffer_release = [](XpAppMemoryDomain, uint8_t* bytes) { std::free(bytes); };
  leaves.protection_index = [](void* raw, uint8_t stage, XpAppText) -> uint8_t {
    assert(stage == 2 || stage == 3);
    if (stage == 2)
      new (raw) File{&zip};
    else
      static_cast<File*>(raw)->~File();
    return 1;
  };
  leaves.file_open_read = [](XpAppText path, XpAppText) -> void* {
    const std::string name(reinterpret_cast<const char*>(path.data), path.len);
    ++liveFiles;
    if (name == "/session.epub") return new File{&zip};
    assert(name == "/session.cache/book.bin");
    ++opened;
    return new File{&cache};
  };
  leaves.file_close = [](void* raw) -> uint8_t {
    auto* file = static_cast<File*>(raw);
    if (file->bytes == &cache) ++closed;
    --liveFiles;
    delete file;
    return 1;
  };
  leaves.scratch_claim = [](uint32_t) -> uint8_t* { return nullptr; };
  leaves.scratch_release = [](const uint8_t*) { std::abort(); };
  leaves.file_size = [](void* raw) -> uint32_t { return static_cast<File*>(raw)->bytes->size(); };
  leaves.file_seek = [](void* raw, uint32_t offset) -> uint8_t {
    auto& file = *static_cast<File*>(raw);
    if (offset > file.bytes->size()) return 0;
    file.offset = offset;
    return 1;
  };
  leaves.file_read = [](void* raw, uint8_t* out, uint32_t count) -> int32_t {
    auto& file = *static_cast<File*>(raw);
    count = std::min<size_t>(count, file.bytes->size() - file.offset);
    std::memcpy(out, file.bytes->data() + file.offset, count);
    file.offset += count;
    return count;
  };
}
}  // namespace book_fixture
