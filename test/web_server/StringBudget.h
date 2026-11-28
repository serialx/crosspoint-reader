#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ESP32 Arduino 3.3.11 WString semantics used by discovery: 14-byte SSO
// threshold, 16-byte heap rounding, retained concat on failure, invalidating +.
// Bookkeeping storage belongs to the host fixture, outside the simulated heap.
class String {
  std::string bytes;
  size_t allocated = 0;
  bool valid = false;

 public:
  inline static size_t live = 0, peak = 0, budget = SIZE_MAX, failAt = SIZE_MAX, attempts = 0;
  inline static std::vector<size_t> requests;
  String() = default;
  String(const char* text) : String(text, text ? std::char_traits<char>::length(text) : 0) {}
  String(const char* text, size_t length) {
    if (text && reserve(length)) bytes.assign(text, length);
  }
  String(unsigned short number) : String(std::to_string(number).c_str()) {}
  String(const String& other) : String(other.c_str(), other.length()) {}
  String(String&& other) noexcept
      : bytes(std::move(other.bytes)), allocated(std::exchange(other.allocated, 0)), valid(other.valid) {
    other.valid = false;
  }
  ~String() { live -= allocated; }
  String& operator=(const char* text) {
    if (!text)
      invalidate();
    else {
      const size_t length = std::char_traits<char>::length(text);
      if (reserve(length))
        bytes.assign(text, length);
      else
        invalidate();
    }
    return *this;
  }
  bool reserve(size_t length) {
    if (valid && (allocated ? allocated - 1 : 14) >= length) return true;
    if (length < 14) {
      live -= allocated;
      allocated = 0;
      valid = true;
      return true;
    }
    const size_t requested = (length + 16) & ~size_t(15);
    requests.push_back(requested);
    const bool fail = attempts++ == failAt || live - allocated + requested > budget;
    if (fail) return false;
    live = live - allocated + requested;
    peak = std::max(peak, live);
    allocated = requested;
    valid = true;
    return true;
  }
  void invalidate() {
    live -= allocated;
    allocated = 0;
    bytes.clear();
    valid = false;
  }
  bool concat(const char* text, size_t length) {
    if (!text) return false;
    if (!length) return true;
    if (!reserve(bytes.size() + length)) return false;
    bytes.append(text, length);
    return true;
  }
  String& operator+=(const String& other) {
    concat(other.c_str(), other.length());
    return *this;
  }
  const char* c_str() const { return valid ? bytes.c_str() : nullptr; }
  size_t length() const { return bytes.size(); }
  bool isEmpty() const { return bytes.empty(); }
};
class StringSumHelper : public String {
 public:
  using String::String;
  StringSumHelper(const String& other) : String(other) {}
};
inline StringSumHelper& operator+(const StringSumHelper& lhs, const String& rhs) {
  auto& result = const_cast<StringSumHelper&>(lhs);
  if (!result.concat(rhs.c_str(), rhs.length())) result.invalidate();
  return result;
}
inline StringSumHelper& operator+(const StringSumHelper& lhs, const char* rhs) {
  auto& result = const_cast<StringSumHelper&>(lhs);
  if (!rhs || !result.concat(rhs, std::char_traits<char>::length(rhs))) result.invalidate();
  return result;
}
