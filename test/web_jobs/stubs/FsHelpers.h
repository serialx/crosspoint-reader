#pragma once
#include <string_view>

#include "Arduino.h"
namespace FsHelpers {
inline std::string normalisePath(std::string_view s) {
  std::vector<std::string_view> parts;
  size_t pos = 0;
  while (pos < s.size()) {
    auto end = s.find('/', pos);
    if (end == s.npos) end = s.size();
    auto p = s.substr(pos, end - pos);
    if (p == "..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!p.empty())
      parts.push_back(p);
    pos = end + 1;
  }
  std::string out;
  for (auto p : parts) {
    out += '/';
    out += p;
  }
  return out;
}
inline bool checkFileExtension(std::string_view p, const char* e) {
  size_t n = strlen(e);
  if (p.size() < n) return false;
  for (size_t i = 0; i < n; ++i)
    if (tolower((unsigned char)p[p.size() - n + i]) != e[i]) return false;
  return true;
}
inline bool hasEpubExtension(std::string_view s) { return checkFileExtension(s, ".epub"); }
inline bool hasTxtExtension(std::string_view s) { return checkFileExtension(s, ".txt"); }
inline bool hasJpgExtension(std::string_view s) {
  return checkFileExtension(s, ".jpg") || checkFileExtension(s, ".jpeg");
}
inline bool hasPngExtension(std::string_view s) { return checkFileExtension(s, ".png"); }
inline bool hasGifExtension(std::string_view s) { return checkFileExtension(s, ".gif"); }
}  // namespace FsHelpers
