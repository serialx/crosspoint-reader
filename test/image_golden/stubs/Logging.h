#pragma once

#define ENABLE_SERIAL_LOG
#define LOG_LEVEL 2

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

inline void imageDiagnosticLog(const char* level, const char* origin, const char* format, ...) {
  if (!std::getenv("IMAGE_DIAGNOSTIC_LOGS")) return;
  std::fprintf(stderr, "%s %s ", level, origin);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::putc('\n', stderr);
}

#define LOG_ERR(origin, ...) imageDiagnosticLog("ERR", origin, __VA_ARGS__)
#define LOG_INF(origin, ...) imageDiagnosticLog("INF", origin, __VA_ARGS__)
#define LOG_DBG(origin, ...) imageDiagnosticLog("DBG", origin, __VA_ARGS__)
