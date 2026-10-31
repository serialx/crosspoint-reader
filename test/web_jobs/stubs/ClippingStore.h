#pragma once
#include "Oracle.h"
struct ClippingStore {
  static bool moveBook(const std::string& a, const std::string& b) {
    oracle::record("move-book %s %s", a.c_str(), b.c_str());
    return oracle::fault != "move";
  }
};
