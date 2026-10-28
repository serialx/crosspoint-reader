#pragma once
#include <cstdint>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  size_t write(uint8_t b) { push_back(static_cast<char>(b)); return 1; }
  size_t write(const uint8_t* p, size_t n) { append(reinterpret_cast<const char*>(p), n); return n; }
};
class Stream {};
