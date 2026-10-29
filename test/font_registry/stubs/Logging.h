#pragma once
#include <cstdio>
#define LOG_ERR(tag, format, ...) std::printf("ERR " tag " " format "\n", ##__VA_ARGS__)
#define LOG_DBG(tag, format, ...) std::printf("DBG " tag " " format "\n", ##__VA_ARGS__)
