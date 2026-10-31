#pragma once
#ifdef XP_LIBRARY_CAPTURE_LOGS
#include <cstdarg>
#include <cstdio>
inline void libraryTestLog(const char* level, const char* origin, const char* format, ...) {
  std::fprintf(stderr, "%s|%s|", level, origin);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
}
#define LOG_DBG(...) libraryTestLog("DBG", __VA_ARGS__)
#define LOG_ERR(...) libraryTestLog("ERR", __VA_ARGS__)
#define LOG_INF(...) libraryTestLog("INF", __VA_ARGS__)
#else

#define LOG_DBG(...) \
  do {               \
  } while (0)
#define LOG_ERR(...) \
  do {               \
  } while (0)
#define LOG_INF(...) \
  do {               \
  } while (0)

#endif
