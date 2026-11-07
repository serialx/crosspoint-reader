#pragma once
#include <cstdio>
struct OracleSerial {
  explicit operator bool() const { return true; }
  void println(const char* text) { std::puts(text); }
  template <class... Args>
  void printf(const char* format, Args... args) {
    std::printf(format, args...);
  }
};
inline OracleSerial logSerial;
#define LOG_ERR(tag, fmt, ...)  \
  do {                          \
    printf("ERR %s ", tag);     \
    printf(fmt, ##__VA_ARGS__); \
    puts("");                   \
  } while (0)
#define LOG_INF(tag, fmt, ...)  \
  do {                          \
    printf("INF %s ", tag);     \
    printf(fmt, ##__VA_ARGS__); \
    puts("");                   \
  } while (0)
#define LOG_DBG(tag, fmt, ...)  \
  do {                          \
    printf("DBG %s ", tag);     \
    printf(fmt, ##__VA_ARGS__); \
    puts("");                   \
  } while (0)
#include <string>
inline bool sanitizeLogHead() {
  puts("sanitize logs");
  return true;
}
inline void clearLastLogs() { puts("clear logs"); }
inline std::string getLastLogs() {
  puts("get logs");
  return "saved logs";
}
