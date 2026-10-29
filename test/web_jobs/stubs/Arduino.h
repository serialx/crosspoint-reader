#pragma once
#include "Oracle.h"
class Stream {
 public:
  virtual ~Stream() = default;
  virtual size_t write(const uint8_t*, size_t n) { return n; }
};
inline void delay(unsigned n) { oracle::record("delay %u", n); }
inline void yield() { oracle::record("yield"); }
struct Esp {
  unsigned getFreeHeap() { return 123456; }
  unsigned getMaxAllocHeap() { return 98765; }
};
inline Esp ESP;

class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(size_t n) : std::string(std::to_string(n)) {}
  char* begin() { return data(); }
  void concat(const char* p, size_t n) { append(p, n); }
  bool isEmpty() const { return empty(); }
  bool startsWith(const char* s) const { return rfind(s, 0) == 0; }
  bool endsWith(const char* s) const {
    size_t n = strlen(s);
    return size() >= n && compare(size() - n, n, s) == 0;
  }
  bool equals(const char* s) const { return *this == s; }
  int lastIndexOf(char c) const {
    auto n = rfind(c);
    return n == npos ? -1 : int(n);
  }
  int indexOf(char c, int start = 0) const {
    auto n = find(c, start);
    return n == npos ? -1 : int(n);
  }
  int indexOf(const char* c, int start = 0) const {
    auto n = find(c, start);
    return n == npos ? -1 : int(n);
  }
  String substring(size_t start, size_t end = npos) const { return substr(start, end == npos ? npos : end - start); }
  char charAt(size_t i) const { return i < size() ? (*this)[i] : 0; }
};
class NetworkClient : public Stream {};
