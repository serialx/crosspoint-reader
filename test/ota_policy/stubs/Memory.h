#pragma once
#include <memory>

#include "Oracle.h"
template <class T>
auto makeUniqueNoThrow() {
  oracle::record("parser-allocate");
  return std::unique_ptr<T>(oracle::fault == "oom" ? nullptr : new (std::nothrow) T);
}
