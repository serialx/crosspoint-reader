#pragma once
#include <HalStorage.h>

#include <cstdarg>
inline void testLog(const char* level, const char* tag, const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  protection_test::trace.push_back(std::string(level) + " " + tag + " " + text);
}
#define LOG_DBG(tag, ...) testLog("DBG", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) testLog("INF", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) testLog("ERR", tag, __VA_ARGS__)
