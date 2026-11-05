#pragma once
#include <memory>
#include <type_traits>
#include <utility>
inline int allocationLimit = -1;
inline bool allocAllowed() {
  if (allocationLimit == 0) return false;
  if (allocationLimit > 0) --allocationLimit;
  return true;
}
template <class T, class... Args>
std::enable_if_t<!std::is_array_v<T>, std::unique_ptr<T>> makeUniqueNoThrow(Args&&... args) {
  return allocAllowed() ? std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...)) : nullptr;
}
template <class T>
std::enable_if_t<std::is_array_v<T>, std::unique_ptr<T>> makeUniqueNoThrow(size_t n) {
  return allocAllowed() ? std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[n]()) : nullptr;
}
