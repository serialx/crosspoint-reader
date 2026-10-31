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
#ifdef ORACLE_FALLIBLE_STRING
  // Model ESP32 WString's 14-byte inline capacity and fallible copy/concat.
  // Platform backing storage is outside the application's allocation gate.
  String() = default;
  String(const char* s) { assignChecked(s, strlen(s)); }
  String(const char* s, size_t n) { assignChecked(s, n); }
  String(const std::string& s) { assignChecked(s.data(), s.size()); }
  String(const String& s) { assignChecked(s.data(), s.size()); }
  String(String&& s) = default;
  String& operator=(String&& s) = default;
  String& operator=(const String& s) {
    if (this != &s) assignChecked(s.data(), s.size());
    return *this;
  }
  String& operator=(const char* s) {
    assignChecked(s, strlen(s));
    return *this;
  }
  friend String operator+(const String& a, const char* b) {
    String out(a);
    if (!out.concat(b, strlen(b))) out.clear();
    return out;
  }
  bool concat(const char* p, size_t n) {
    if (size() + n > targetCapacity && allocation_fault::fail()) return false;
    allocation_fault::PlatformScope scope;
    append(p, n);
    if (size() > targetCapacity) targetCapacity = (size() + 16) / 16 * 16 - 1;
    return true;
  }

 private:
  size_t targetCapacity = 14;
  void assignChecked(const char* s, size_t n) {
    if (n > targetCapacity && allocation_fault::fail()) {
      clear();
      return;
    }
    allocation_fault::PlatformScope scope;
    assign(s, n);
    if (n > targetCapacity) targetCapacity = (n + 16) / 16 * 16 - 1;
  }

 public:
#else
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  void concat(const char* p, size_t n) { append(p, n); }
#endif
  String(size_t n) : std::string(std::to_string(n)) {}
  char* begin() { return data(); }
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
