#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

class String : public std::string {
 public:
  using std::string::string;
  inline static size_t failAllocationSize = SIZE_MAX;
  inline static size_t failedAllocations = 0;
  String() = default;
  String(const char* s, size_t n) {
    if (s && reserve(n)) assign(s, n);
  }
  String(const std::string& s) : std::string(s) {}
  template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
  explicit String(T n) : std::string(std::to_string(n)) {}
  bool isEmpty() const { return empty(); }
  bool equals(const String& s) const { return size() == s.size() && std::strcmp(c_str(), s.c_str()) == 0; }
  bool equals(const char* s) const { return s ? std::strcmp(c_str(), s) == 0 : empty() || (*this)[0] == 0; }
  friend bool operator==(const String& s, const char* text) { return s.equals(text); }
  bool startsWith(const String& s) const { return starts_with(s); }
  bool endsWith(const String& s) const { return ends_with(s); }
  int indexOf(char c, size_t from = 0) const {
    if (from >= size()) return -1;
    const char* found = std::strchr(c_str() + from, c);
    return found ? static_cast<int>(found - c_str()) : -1;
  }
  int indexOf(const char* s, size_t from = 0) const {
    if (from >= size()) return -1;
    const char* found = std::strstr(c_str() + from, s);
    return found ? static_cast<int>(found - c_str()) : -1;
  }
  int lastIndexOf(char c) const { return index(rfind(c)); }
  String substring(size_t first, size_t last = npos) const {
    if (first > last) std::swap(first, last);
    if (first >= size()) return {};
    return substr(first, std::min(last, size()) - first);
  }
  void trim() {
    size_t first = 0, last = size();
    while (first < last && std::isspace(static_cast<unsigned char>((*this)[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>((*this)[last - 1]))) last--;
    *this = substr(first, last - first);
  }
  void toLowerCase() {
    for (auto& c : *this) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  void replace(char from, char to) { std::replace(begin(), end(), from, to); }
  int32_t toInt() const {
    const auto n = std::strtoll(c_str(), nullptr, 10);
    return static_cast<int32_t>(std::clamp<int64_t>(n, INT32_MIN, INT32_MAX));
  }
  bool reserve(size_t n) {
    if (n == failAllocationSize) {
      failAllocationSize = SIZE_MAX;
      ++failedAllocations;
      return false;
    }
    std::string::reserve(n);
    return true;
  }
  size_t write(uint8_t c) {
    push_back(static_cast<char>(c));
    return 1;
  }
  size_t write(const uint8_t* p, size_t n) {
    append(reinterpret_cast<const char*>(p), n);
    return n;
  }

 private:
  static int index(size_t n) { return n == npos ? -1 : static_cast<int>(n); }
};
