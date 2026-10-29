#pragma once
#include <Arduino.h>
#include <string>
struct String { std::string value; const char* c_str() const { return value.c_str(); } };
struct Print {
 virtual ~Print() = default;
 virtual size_t write(const uint8_t*, size_t) { return 0; }
 virtual size_t write(uint8_t) { return 0; }
};
