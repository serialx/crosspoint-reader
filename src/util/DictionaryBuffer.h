#pragma once

#include <Memory.h>

#include <cstring>
#include <string_view>

// Lookup output is transferred to the viewer without allocating a second copy.
class DictionaryBuffer {
 public:
  DictionaryBuffer() = default;
  DictionaryBuffer(DictionaryBuffer&& other) noexcept { *this = std::move(other); }
  DictionaryBuffer& operator=(DictionaryBuffer&& other) noexcept {
    bytes = std::move(other.bytes);
    length = std::exchange(other.length, 0);
    allocated = std::exchange(other.allocated, 0);
    return *this;
  }

  size_t size() const { return length; }
  size_t capacity() const { return allocated; }
  const char* data() const { return bytes ? bytes.get() : ""; }
  std::string_view view() const { return {data(), length}; }
  bool reserve(size_t count) {
    if (count <= allocated) return true;
    auto next = makeUniqueNoThrow<char[]>(count + 1);
    if (!next) return false;
    if (length) memcpy(next.get(), bytes.get(), length);
    bytes = std::move(next);
    allocated = count;
    return true;
  }
  char* resize(size_t count) {
    if (!reserve(count)) return nullptr;
    length = count;
    if (bytes) bytes[length] = '\0';
    return bytes.get();
  }
  void append(const char* source, size_t count) {
    // The group owner reserves the complete labelled entry before appending.
    memcpy(bytes.get() + length, source, count);
    length += count;
    bytes[length] = '\0';
  }

 private:
  std::unique_ptr<char[]> bytes;
  size_t length = 0;
  size_t allocated = 0;
};
