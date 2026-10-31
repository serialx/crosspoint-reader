#pragma once
#include "Oracle.h"
namespace obfuscation {
inline std::string obfuscateToBase64(const std::string& s) { return s; }
inline std::string deobfuscateFromBase64(const char* s, size_t, bool* ok, bool* tooLong) {
  *ok = true;
  *tooLong = false;
  return s;
}
}  // namespace obfuscation
