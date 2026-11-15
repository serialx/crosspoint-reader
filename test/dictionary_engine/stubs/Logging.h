#pragma once
#include <cstdarg>
#include <cstdio>
inline void dictionaryLog(const char* level, const char* tag, const char* format, ...) {
  std::printf("%s %s ", level, tag);
  va_list args;
  va_start(args, format);
  std::vprintf(format, args);
  va_end(args);
  std::puts("");
}
#define LOG_ERR(tag, ...) dictionaryLog("ERR", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) dictionaryLog("INF", tag, __VA_ARGS__)
#define LOG_DBG(tag, ...) dictionaryLog("DBG", tag, __VA_ARGS__)
