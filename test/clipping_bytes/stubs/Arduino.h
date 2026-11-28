#pragma once
#include <cstdint>
#include <string>

// Only the Arduino surface the extracted SDK bodies touch.
struct HostSerial {
  explicit operator bool() const { return false; }
  template <typename... T>
  void printf(const char*, T...) {}
  void println(const char*) {}
};
inline HostSerial Serial;
inline unsigned long millis() { return 0; }

class String {
  std::string value;

 public:
  String() = default;
  String(const char* s) : value(s ? s : "") {}
  String(const std::string& s) : value(s) {}
  const char* c_str() const { return value.c_str(); }
  unsigned int length() const { return value.size(); }
};
