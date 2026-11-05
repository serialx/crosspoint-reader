#pragma once
#include <cstdarg>
#include <cstdio>

inline void diagnosticLog(const char* level, const char* tag, const char* format, ...) {
  std::printf("%s\t%s\t", level, tag);
  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);
  std::putchar('\n');
}
#define LOG_ERR(tag, fmt, ...) diagnosticLog("ERR", tag, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INF(tag, fmt, ...) diagnosticLog("INF", tag, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_DBG(tag, fmt, ...) diagnosticLog("DBG", tag, fmt __VA_OPT__(, ) __VA_ARGS__)
