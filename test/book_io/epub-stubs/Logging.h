#pragma once
#include <cstdarg>
#include <cstdio>
inline void bookLog(const char* level, const char* origin, const char* fmt, ...) {
  printf("log %s %s ", level, origin);
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  puts("");
}
#define LOG_ERR(tag, ...) bookLog("ERR", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) bookLog("INF", tag, __VA_ARGS__)
#define LOG_DBG(tag, ...) bookLog("DBG", tag, __VA_ARGS__)
