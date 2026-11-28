#pragma once
#include "Oracle.h"
namespace HttpDownloader {
inline bool fetchUrl(const std::string& url, const std::function<bool(const uint8_t*, size_t)>& sink) {
  bool release = url.find("api.github") != std::string::npos;
  oracle::record("fetch %s", url.c_str());
  oracle::boundary();
  if (oracle::fault == (release ? "manifest-fetch" : "fetch")) return false;
  const auto* bytes = release ? reinterpret_cast<const uint8_t*>(oracle::manifest.data()) : oracle::image.data();
  size_t n = release ? oracle::manifest.size() : oracle::image.size();
  for (size_t off = 0; off < n; off += oracle::chunk) {
    size_t take = std::min(oracle::chunk, n - off);
    if (!sink(bytes + off, take)) {
      oracle::record("fetch-cancel");
      return false;
    }
    if (!release && oracle::fault == "abort") return false;
  }
  return true;
}
}  // namespace HttpDownloader
