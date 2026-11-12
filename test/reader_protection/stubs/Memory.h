#pragma once
#include <cstring>
#include <memory>
#include <new>
#include <typeinfo>
#include <utility>
namespace protection_test {
inline int allocationFailure = 0;
}
template <class T, class... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  const char* name = typeid(T).name();
  if (protection_test::allocationFailure == 1 && strstr(name, "ProtectedBook") && !strstr(name, "Decryptor"))
    return nullptr;
  if (protection_test::allocationFailure == 2 && strstr(name, "ProtectedBookDecryptor")) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <class T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]());
}
