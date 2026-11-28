#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace logging_stub {
inline std::vector<std::string>* captured = nullptr;
inline void sink(const char* level, const char* origin, const char* format, ...) {
  if (!captured) return;
  char text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  captured->push_back(std::string(level) + "\t" + origin + "\t" + text);
}
struct Capture {
  std::vector<std::string> lines;
  Capture() { captured = &lines; }
  ~Capture() { captured = nullptr; }
};
}  // namespace logging_stub
#define LOG_ERR(origin, ...) logging_stub::sink("ERR", origin, __VA_ARGS__)
#define LOG_INF(origin, ...) logging_stub::sink("INF", origin, __VA_ARGS__)
#define LOG_DBG(origin, ...) logging_stub::sink("DBG", origin, __VA_ARGS__)
