#pragma once

#include <cstddef>
#include <string>

class String {
 public:
  explicit String(std::string value) : value(std::move(value)) {}
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }

 private:
  std::string value;
};
