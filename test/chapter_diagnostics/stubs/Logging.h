#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace ChapterLogs {
inline std::vector<std::string> lines;
inline void log(const char* level, const char* origin, const char* format, ...) {
  char text[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  lines.emplace_back(std::string(level) + ":" + origin + ":" + text);
}
}  // namespace ChapterLogs
#define LOG_ERR(origin, ...) ChapterLogs::log("ERR", origin, __VA_ARGS__)
#define LOG_INF(origin, ...) ChapterLogs::log("INF", origin, __VA_ARGS__)
#define LOG_DBG(origin, ...) ChapterLogs::log("DBG", origin, __VA_ARGS__)
