#pragma once
#include <memory>
template<class T> auto makeUniqueNoThrow(size_t n) { return std::unique_ptr<T>(new(std::nothrow) std::remove_extent_t<T>[n]); }
