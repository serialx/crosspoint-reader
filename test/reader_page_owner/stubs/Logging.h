#pragma once
void pageOwnerLog(const char* tag, const char* format, ...);
#define LOG_ERR(tag, fmt, ...) pageOwnerLog(tag, fmt __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INF(tag, fmt, ...) ((void)0)
#define LOG_DBG(tag, fmt, ...) ((void)0)
