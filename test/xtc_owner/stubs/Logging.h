#pragma once
#include <cstdio>
#include <string>
#include <vector>
namespace xtcowner {
inline std::vector<std::string> logs;
}
template <class... Args>
void xtcOwnerLog(const char* level, const char* tag, const char* format, Args... args) {
  char line[1024];
  if constexpr (sizeof...(args))
    std::snprintf(line, sizeof(line), format, args...);
  else
    std::snprintf(line, sizeof(line), "%s", format);
  xtcowner::logs.emplace_back(std::string(level) + " " + tag + " " + line);
}
#define LOG_DBG(tag, ...) xtcOwnerLog("DBG", tag, __VA_ARGS__)
#define LOG_INF(tag, ...) xtcOwnerLog("INF", tag, __VA_ARGS__)
#define LOG_ERR(tag, ...) xtcOwnerLog("ERR", tag, __VA_ARGS__)
