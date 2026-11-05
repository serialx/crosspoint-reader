#pragma once

#include <cstddef>
#include <cstdint>

enum class InflateStatus { Ok, Done, Error };

// Raw deflate with inline compact Rust state; one-shot decoding allocates nothing.
// One-shot calls use one contiguous destination holding all previous output.
// Streaming uses a caller-owned 32 KiB ring, allocated before smaller buffers.
class InflateReader {
 public:
  static constexpr size_t RING_BYTES = 32768;
  using FillFn = size_t (*)(void*, const uint8_t**);
  InflateReader() = default;
  ~InflateReader();
  InflateReader(const InflateReader&) = delete;
  InflateReader& operator=(const InflateReader&) = delete;
  bool init(bool streaming = false);
  bool initWithRing(uint8_t* ring);
  void deinit();
  void setSource(const uint8_t* src, size_t len);
  // Return bytes available until the next fill call, or zero at end of input.
  void setFill(FillFn fn, void* context);
  bool read(uint8_t* dest, size_t len);
  InflateStatus readAtMost(uint8_t* dest, size_t maxLen, size_t* produced);

 private:
  // Retain the original reader footprint and alignment: moving its global owner
  // shifts heap boundaries and can lower per-heap minimum-free counters.
  // Rust uses 1076 B on C3; init checks this fixed slot with no heap fallback.
  alignas(void*) uint8_t state[1260] = {};
  uint8_t* ringBuffer = nullptr;
  const uint8_t* input = nullptr;
  size_t available = 0;
  size_t position = 0;
  FillFn fill = nullptr;
  void* context = nullptr;
  uint8_t* outputStart = nullptr;
  bool ownsRing = false;
  bool exhausted = false;
  bool ready = false;
  bool finished = false;
};
