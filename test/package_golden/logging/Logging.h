#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

inline bool packageCaptureLogs = false;
inline std::vector<std::string> packageLogs;
inline void packageTestLog(const char* level, const char* tag, const char* format, ...) {
  va_list args;
  va_start(args, format);
  if (packageCaptureLogs) {
    char text[4096];
    std::vsnprintf(text, sizeof(text), format, args);
    packageLogs.push_back(std::string(level) + "|" + tag + "|" + text);
  } else {
    std::fprintf(stderr, "%s|%s|", level, tag);
    std::vfprintf(stderr, format, args);
    std::fputc('\n', stderr);
  }
  va_end(args);
}
#define LOG_DBG(tag, format, ...) packageTestLog("DBG", tag, format, ##__VA_ARGS__)
#define LOG_ERR(tag, format, ...) packageTestLog("ERR", tag, format, ##__VA_ARGS__)
#define LOG_INF(tag, format, ...) packageTestLog("INF", tag, format, ##__VA_ARGS__)
