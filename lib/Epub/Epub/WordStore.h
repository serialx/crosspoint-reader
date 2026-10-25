#pragma once

#include <xp_ffi.h>

#include <cstdint>
#include <cstring>
#include <string_view>

// Bump arena for layout-time word text. Words are copied in NUL-terminated so
// views can feed C string APIs (drawText, startsWithRtl) directly.
//
// Text accumulates into fixed 2KB chunks; a chunk is freed once every word in
// it has been released. Words are consumed FIFO as lines are extracted, so
// chunks retire in step with layout even when one paragraph spans a whole
// chapter. Uniform 2KB requests are deliberate: they stay allocatable under
// the fragmentation that defeats per-word variable-size allocations.
//
// Every allocation is nothrow. append() reports failure instead of aborting
// (-fno-exceptions turns a failed implicit allocation into abort()), giving
// the layout path a single detectable OOM choke point.
class WordStore {
  friend class ParsedText;

 public:
  WordStore();
  ~WordStore();
  WordStore(const WordStore&) = delete;
  WordStore& operator=(const WordStore&) = delete;

  struct StoredWord {
    uint32_t chunk = 0;
    uint16_t off = 0;
    uint16_t len = 0;
  };

  // Copies [text, text+len) plus a NUL terminator into the arena.
  // Returns false on OOM; out is untouched in that case.
  bool append(const char* text, size_t len, StoredWord& out);

  // NUL-terminated view of a stored word.
  std::string_view view(const StoredWord& w) const { return {charsAt(w), w.len}; }
  const char* cstr(const StoredWord& w) const { return charsAt(w); }

  // A suffix of an existing word (shares its bytes and trailing NUL). The
  // suffix inherits the original's release obligation: release exactly one of
  // the two, never both.
  static StoredWord suffix(const StoredWord& w, size_t byteOffset) {
    return {w.chunk, static_cast<uint16_t>(w.off + byteOffset), static_cast<uint16_t>(w.len - byteOffset)};
  }

  // Declares a stored word consumed. Frees its chunk once no other live word
  // points into it.
  void release(const StoredWord& w);

  // Chunk enumeration for whole-paragraph codepoint scans (SD font advance
  // prewarm). Chunks hold consecutive NUL-terminated words; data may be null
  // for retired chunks.
  size_t chunkCount() const { return xp_word_store_chunk_count(handle()); }
  const char* chunkData(size_t i) const { return xp_word_store_chunk_data(handle(), i); }
  size_t chunkUsed(size_t i) const { return xp_word_store_chunk_used(handle(), i); }

 private:
  // Caller-owned opaque Rust state; no handle allocation in the paragraph hot path.
  alignas(void*) uint8_t state[4 * sizeof(void*)];
  XpWordStore* handle() { return reinterpret_cast<XpWordStore*>(state); }
  const XpWordStore* handle() const { return reinterpret_cast<const XpWordStore*>(state); }
  const char* charsAt(const StoredWord& w) const { return xp_word_store_cstr(handle(), {w.chunk, w.off, w.len}); }
};

// Shared fallible allocation hooks for Rust paragraph storage.
const XpParaAllocator* paragraphAllocator();
