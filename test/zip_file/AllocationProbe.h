#pragma once
#include <Memory.h>

#include <array>
#include <cstdlib>

namespace ziptest {
inline bool captureAllocations = false;
inline size_t allocationCount = 0, allocationFail = SIZE_MAX;
inline std::array<size_t, 16> allocationSizes{};
inline bool request(size_t size) {
  if (!captureAllocations) return true;
  const auto index = allocationCount++;
  if (index < allocationSizes.size()) allocationSizes[index] = size;
  return index != allocationFail;
}
inline void* allocate(size_t size) { return request(size) ? std::malloc(size) : nullptr; }
template <typename T>
std::unique_ptr<T> unique(size_t size) {
  return request(size * sizeof(std::remove_extent_t<T>)) ? makeUniqueNoThrow<T>(size) : nullptr;
}
}  // namespace ziptest
