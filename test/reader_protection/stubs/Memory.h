#pragma once
#include <cstring>
#include <memory>
#include <new>
#include <typeinfo>
#include <utility>
namespace protection_test {
inline int allocationFailure = 0;
inline size_t bufferFailure = 0, bufferAttempts = 0;
inline bool failBuffer() { return ++bufferAttempts == bufferFailure; }
}  // namespace protection_test
template <class T, class... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  const char* name = typeid(T).name();
  if (protection_test::allocationFailure == 3 && strstr(name, "HalFile")) {
    protection_test::allocationFailure = 0;  // One failure, then recovery.
    return nullptr;
  }
  if (protection_test::allocationFailure == 1 && strstr(name, "ProtectedBook") && !strstr(name, "Decryptor"))
    return nullptr;
  if (protection_test::allocationFailure == 2 && strstr(name, "ProtectedBookDecryptor")) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <class T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (protection_test::failBuffer()) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]());
}
