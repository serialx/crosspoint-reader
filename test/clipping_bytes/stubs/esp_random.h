#pragma once
#include <cstddef>
#include <cstdint>

// Substituted randomness. prepareSync() fills clipping ids from esp_fill_random;
// pinning a fixed stream keeps the written bytes comparable across runs.
namespace clip_test_random {
inline uint32_t state = 0x9e3779b9u;

inline void reset() { state = 0x9e3779b9u; }

inline uint8_t next() {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<uint8_t>(state >> 24);
}
}  // namespace clip_test_random

inline void esp_fill_random(void* buffer, const size_t size) {
  auto* bytes = static_cast<uint8_t*>(buffer);
  for (size_t i = 0; i < size; ++i) bytes[i] = clip_test_random::next();
}
