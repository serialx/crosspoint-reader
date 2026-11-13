#pragma once
#include <HalStorage.h>

#include <cstdarg>
extern "C" void cssTestFailAllocation(size_t);
namespace protection_test {
inline size_t rustFailure = SIZE_MAX;
inline bool armRustFailure = false;
}  // namespace protection_test
inline void testLog(const char* level, const char* tag, const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (protection_test::armRustFailure && std::string(tag) == "CPRO" && std::string(text).starts_with("open:")) {
    protection_test::armRustFailure = false;
    cssTestFailAllocation(protection_test::rustFailure);
  }
  protection_test::trace.push_back(std::string(level) + " " + tag + " " + text);
}
#define LOG_DBG(tag, ...) testLog("DBG", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) testLog("INF", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) testLog("ERR", tag, __VA_ARGS__)
