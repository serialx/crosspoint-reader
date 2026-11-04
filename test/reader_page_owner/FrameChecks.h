#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace frameChecks {
inline bool allocating = false, loan = false;
inline size_t requests = 0, failAt = SIZE_MAX;
inline std::array<void*, 4> live{};
inline unsigned borrows = 0, successes = 0, imageBorrows = 0, sourceRejections = 0;
inline std::array<unsigned, 4> allocationFailures{};
inline unsigned cacheReleases = 0, windowClears = 0, detaches = 0;
inline unsigned sequence = 0, detachAt = 0, windowClearAt = 0, prewarmDropAt = 0, cacheReleaseAt = 0;
inline uint8_t* window = nullptr;
inline void reset() {
  assert(!allocating && !loan && !window);
  for (auto* p : live) assert(!p);
  requests = 0;
  failAt = SIZE_MAX;
  sequence = detachAt = windowClearAt = prewarmDropAt = cacheReleaseAt = 0;
}
}  // namespace frameChecks
