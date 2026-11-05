#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace store_test {
inline std::vector<std::string> logs;
inline void log(const char* level, const char* origin, const char* format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  logs.push_back(std::string(level) + ":" + origin + ":" + buffer);
}
}  // namespace store_test
#define LOG_ERR(origin, ...) store_test::log("ERR", origin, __VA_ARGS__)
#define LOG_DBG(origin, ...) store_test::log("DBG", origin, __VA_ARGS__)
#define LOG_INF(origin, ...) store_test::log("INF", origin, __VA_ARGS__)
