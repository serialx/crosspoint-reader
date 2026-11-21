#pragma once

#include <cstddef>
#include <cstdint>

enum class InflateStatus { Ok, Done, Error };

// Raw deflate with inline compact Rust state; one-shot decoding allocates nothing.
// One-shot calls use one contiguous destination holding all previous output.
// Streaming keeps a 32 KiB ring, contiguous or split over four caller-owned segments.
class InflateReader {
 public:
  static constexpr size_t RING_SEGMENT_BYTES = 8192;
  static constexpr size_t RING_SEGMENTS = 4;
  static constexpr size_t RING_BYTES = RING_SEGMENTS * RING_SEGMENT_BYTES;
  using FillFn = size_t (*)(void*, const uint8_t**);
  InflateReader() = default;
  ~InflateReader();
  InflateReader(const InflateReader&) = delete;
  InflateReader& operator=(const InflateReader&) = delete;
  bool init(bool streaming = false);
  bool initWithRing(uint8_t* ring);
  // Each buffer provides RING_SEGMENT_BYTES writable bytes and outlives the reader.
  bool initWithSegments(uint8_t* const (&segments)[RING_SEGMENTS]);
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
  alignas(void*) uint8_t state[1260 - sizeof(void*) * RING_SEGMENTS] = {};
  uint8_t* segmentTable[RING_SEGMENTS]{};
  enum class Window : uint8_t { None, Owned, Borrowed, Segmented };
  void* ringBuffer = nullptr;
  const uint8_t* input = nullptr;
  size_t available = 0;
  size_t position = 0;
  FillFn fill = nullptr;
  void* context = nullptr;
  uint8_t* outputStart = nullptr;
  Window window = Window::None;
  bool exhausted = false;
  bool ready = false;
  bool finished = false;
};
