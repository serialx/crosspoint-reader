#pragma once
#include "Oracle.h"
class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  void remove(size_t n) { erase(n); }
  bool reserve(size_t n) {
    if (oracle::fault == "reserve") return false;
    std::string::reserve(n);
    return true;
  }
  bool concat(const char* p, size_t n) {
    if (oracle::fault == "concat") return false;
    append(p,n);
    return true;
  }
};
