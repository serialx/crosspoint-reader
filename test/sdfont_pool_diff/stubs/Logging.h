#pragma once

#include <cstdarg>
#include <cstdio>

inline void sdPoolLog(const char* level, const char* origin, const char* format, ...) {
  std::printf("LOG %s %s ", level, origin);
  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);
  std::putchar('\n');
}

#define LOG_ERR(origin, ...) sdPoolLog("ERR", origin, __VA_ARGS__)
#define LOG_INF(origin, ...) sdPoolLog("INF", origin, __VA_ARGS__)
#define LOG_DBG(origin, ...) sdPoolLog("DBG", origin, __VA_ARGS__)
