#pragma once
#include "Arduino.h"
#ifdef LIBRARY_HEADER_FAULT_TEST
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
inline std::vector<std::string> headerLogs;
inline void headerLog(const char* level, const char* tag, const char* format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  headerLogs.emplace_back(std::string(level) + ":" + tag + ":" + message);
}
#define LOG_DBG(...) headerLog("D", __VA_ARGS__)
#define LOG_INF(...) headerLog("I", __VA_ARGS__)
#define LOG_ERR(...) headerLog("E", __VA_ARGS__)
#else
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#endif
