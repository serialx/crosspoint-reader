#pragma once
#include <HalStorage.h>
#include <Memory.h>
#include <xp_settings.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace serialization {

// Sequential buffered wrappers over HalFile.
//
// SdFat keeps ONE shared 512-byte sector cache per volume, so interleaving small
// reads/writes across two or more files evicts and reloads that sector on nearly
// every call -- each 4-byte pod becomes a full SD transaction (measured: 31s to
// stream ~200KB through BookMetadataCache::buildBookBin on a 1,732-spine EPUB).
// Batching into chunk-sized transfers keeps each file at sequential SD speed.
//
// Heap: one fixed buffer per wrapper, allocated once at construction and freed at
// scope exit. If the allocation fails the wrapper degrades to unbuffered
// passthrough -- correct, just slow -- so callers never need an OOM path.
//
// Constraint: the wrapper must be the file's ONLY accessor while alive (it tracks
// the underlying position itself); mixing direct HalFile calls in desynchronizes it.

inline XpStoreFileOps fileOps(HalFile& file) {
  return {&file,
          [](void* ctx, uint8_t* dst, size_t len) -> int32_t { return static_cast<HalFile*>(ctx)->read(dst, len); },
          [](void* ctx, const uint8_t* src, size_t len) { return static_cast<HalFile*>(ctx)->write(src, len); },
          [](void* ctx, size_t pos) { return static_cast<HalFile*>(ctx)->seek(pos); }};
}

class BufferedFileWriter {
 public:
  BufferedFileWriter(HalFile& file, const size_t capacity)
      : file(file),
        buf(makeUniqueNoThrow<uint8_t[]>(capacity)),
        cap(buf ? capacity : 0),
        state{0, file.position(), true} {}
  ~BufferedFileWriter() { flush(); }
  BufferedFileWriter(const BufferedFileWriter&) = delete;
  BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;
  void write(const void* src, size_t len) {
    const auto ops = fileOps(file);
    xp_store_buffer_write(&state, buf.get(), cap, &ops, static_cast<const uint8_t*>(src), len);
  }
  size_t position() const { return state.pos; }
  bool flush() {
    const auto ops = fileOps(file);
    return xp_store_buffer_flush(&state, buf.get(), &ops);
  }

 private:
  HalFile& file;
  std::unique_ptr<uint8_t[]> buf;
  const size_t cap;
  XpStoreWriter state;
};

class BufferedFileReader {
 public:
  BufferedFileReader(HalFile& file, const size_t capacity)
      : file(file),
        buf(makeUniqueNoThrow<uint8_t[]>(capacity)),
        cap(buf ? capacity : 0),
        state{0, 0, file.position()} {}
  BufferedFileReader(const BufferedFileReader&) = delete;
  BufferedFileReader& operator=(const BufferedFileReader&) = delete;
  size_t read(void* dst, size_t len) {
    const auto ops = fileOps(file);
    return xp_store_buffer_read(&state, buf.get(), cap, &ops, static_cast<uint8_t*>(dst), len);
  }
  size_t position() const { return state.start + state.off; }
  bool seek(size_t target) {
    const auto ops = fileOps(file);
    return xp_store_buffer_seek(&state, cap, &ops, target);
  }

 private:
  HalFile& file;
  std::unique_ptr<uint8_t[]> buf;
  const size_t cap;
  XpStoreReader state;
};

// serialization:: overloads mirroring the HalFile ones in Serialization.h.
template <typename T>
void writePod(BufferedFileWriter& out, const T& value) {
  out.write(&value, sizeof(T));
}

template <typename T>
void readPod(BufferedFileReader& in, T& value) {
  in.read(&value, sizeof(T));
}

inline void writeString(BufferedFileWriter& out, const std::string& s) {
  const uint32_t len = s.size();
  writePod(out, len);
  out.write(s.data(), len);
}

inline void readString(BufferedFileReader& in, std::string& s) {
  uint32_t len;
  readPod(in, len);
  s.resize(len);
  if (len > 0) {
    in.read(&s[0], len);
  }
}

}  // namespace serialization
