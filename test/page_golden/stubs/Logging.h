#pragma once
#include <Arduino.h>

#include <cstdio>
#include <cstring>
#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(tag, format, ...)                                    \
  do {                                                               \
    if (std::strncmp(format, "!! Outside range", 16) != 0)           \
      std::fprintf(stderr, "[%s] " format "\n", tag, ##__VA_ARGS__); \
  } while (false)
