#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

static bool inflateLogging = true;
static std::vector<std::string> inflateEvents;
namespace HalMemory {
struct HeapStats {
  size_t freeBytes, totalBytes, minFreeBytes, largestBlockBytes;
};
inline HeapStats getDefaultHeap() {
  inflateEvents.push_back("default:" + std::to_string(nativeLive));
  return {11112, 22223, 0, 3334};
}
inline HeapStats getPsramHeap() {
  inflateEvents.push_back("psram:" + std::to_string(nativeLive));
  return {5556, 7778, 0, 0};
}
}  // namespace HalMemory
static void inflateLog(const char* origin, const char* format, ...) {
  char buffer[256];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  inflateEvents.push_back(std::string(origin) + ":" + buffer);
}
class InflateStream {
 public:
  static void logAllocationFailure(const char*, size_t);
};
#define LOG_ERR(origin, ...)                             \
  do {                                                   \
    if (inflateLogging) inflateLog(origin, __VA_ARGS__); \
  } while (0)
// ACTUAL_INFLATE_OOM_HELPER
#undef LOG_ERR
