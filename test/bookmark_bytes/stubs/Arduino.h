#pragma once
#include <cstdint>
#include <string>

// Successful String operations used by the production ArduinoJson adapters.
class String {
  std::string value;

 public:
  String() = default;
  String(const char* s) : value(s ? s : "") {}
  String(const std::string& s) : value(s) {}
  String& operator=(const char* s) {
    value = s ? s : "";
    return *this;
  }
  String& operator+=(char c) {
    value += c;
    return *this;
  }
  bool concat(const char* s) {
    value += s;
    return true;
  }
  bool concat(const char* s, unsigned int n) {
    value.append(s, n);
    return true;
  }
  const char* c_str() const { return value.c_str(); }
  unsigned int length() const { return value.size(); }
  bool isEmpty() const { return value.empty(); }
  friend String operator+(const String& s, const char* tail) { return String(s.value + tail); }
};
struct HostSerial {
  explicit operator bool() const { return false; }
  template <typename... T>
  void printf(const char*, T...) {}
  void println(const char*) {}
};
inline HostSerial Serial;
inline unsigned long millis() { return 0; }
