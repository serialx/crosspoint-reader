#pragma once
#include "Oracle.h"
#define LOG_ERR(tag, fmt, ...) oracle::record("ERR " tag " " fmt, ##__VA_ARGS__)
#define LOG_INF(tag, fmt, ...) oracle::record("INF " tag " " fmt, ##__VA_ARGS__)
#define LOG_DBG(tag, fmt, ...) oracle::record("DBG " tag " " fmt, ##__VA_ARGS__)
