#pragma once

#include <Memory.h>

#include <cstdio>
#include <functional>
#include <string_view>

namespace SleepCoverCache {
struct Paths {
  char final[sizeof("/.crosspoint/sleep_18446744073709551615.bmp")];
  char temp[sizeof(final) + 4];
};

inline bool makePaths(Paths& paths, std::string_view source, size_t bytes, uint32_t modified, int width, int height,
                      bool crop, bool originalThresholds) {
  // The longest selected name is 255 bytes, prefixed by "/.sleep/".
  if (source.size() > 255 + sizeof("/.sleep/") - 1) return false;
  const size_t capacity = source.size() + 64;
  auto key = makeUniqueNoThrow<char[]>(capacity);
  if (!key) return false;
  const int length =
      snprintf(key.get(), capacity, "%.*s|%zu|%lu|%dx%d|%c%s", static_cast<int>(source.size()), source.data(), bytes,
               static_cast<unsigned long>(modified), width, height, crop ? 'c' : 'f', originalThresholds ? "|t" : "");
  if (length < 0 || static_cast<size_t>(length) >= capacity) return false;
  const size_t hash = std::hash<std::string_view>{}({key.get(), static_cast<size_t>(length)});
  snprintf(paths.final, sizeof(paths.final), "/.crosspoint/sleep_%zu.bmp", hash);
  snprintf(paths.temp, sizeof(paths.temp), "%s.tmp", paths.final);
  return true;
}
}  // namespace SleepCoverCache
