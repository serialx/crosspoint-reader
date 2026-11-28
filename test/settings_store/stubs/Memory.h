#pragma once
#include <memory>
namespace store_test {
inline bool failAlloc = false;
}
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow(size_t n) {
  return store_test::failAlloc ? nullptr : std::make_unique<T>(n);
}
