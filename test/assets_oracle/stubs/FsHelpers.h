#pragma once
#include <cstdio>

#include "../../image_golden/stubs/FsHelpers.h"
namespace FsHelpers {
inline void sanitizePathComponentForFat32(const char* src, char* dst, size_t size) {
  std::snprintf(dst, size, "%s", src);
}
}  // namespace FsHelpers
