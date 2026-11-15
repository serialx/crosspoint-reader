#pragma once
#include <string>
class String {
 public:
  String() = default;
  explicit String(const char* text) : value(text ? text : "") {}
  explicit String(const std::string& text) : value(text) {}
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }

 private:
  std::string value;
};
