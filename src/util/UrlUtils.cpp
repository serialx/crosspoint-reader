#include "UrlUtils.h"

#include <HttpText.h>
#include <strings.h>

#include <cctype>
#include <cstring>

namespace UrlUtils {
std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_url_build_retire_c2(httptransfer::nativeText(serverUrl), httptransfer::nativeText(path), output,
                           &httptransfer::textLeaves);
  });
}

bool isServerPath(std::string_view path) {
  return !path.empty() && path[0] == '/' && (path.size() == 1 || path[1] != '/');
}

std::string_view origin(std::string_view requestUrl) {
  const size_t scheme = requestUrl.find("://");
  if (scheme == std::string_view::npos) return {};
  return requestUrl.substr(0, requestUrl.find_first_of("/?#", scheme + 3));
}

bool resolveServerPath(std::string& path, const std::string& requestUrl) {
  const auto base = origin(requestUrl);
  if (!isServerPath(path) || base.empty()) return false;
  path.insert(0, base.data(), base.size());
  return true;
}

bool isForeignHost(const char* hostHeader) {
  if (hostHeader == nullptr) return false;  // a String whose allocation failed
  const size_t len = strcspn(hostHeader, ":");
  if (len == 0 || hostHeader[0] == '[') return false;  // no host, or an IPv6 literal
  constexpr char LOCAL_SUFFIX[] = ".local";
  constexpr size_t SUFFIX_LEN = sizeof(LOCAL_SUFFIX) - 1;
  if (len >= SUFFIX_LEN && strncasecmp(hostHeader + len - SUFFIX_LEN, LOCAL_SUFFIX, SUFFIX_LEN) == 0) return false;
  bool hasDot = false;
  bool allDigits = true;
  for (size_t i = 0; i < len; i++) {
    const char c = hostHeader[i];
    if (c == '.') {
      hasDot = true;
    } else if (!isdigit(static_cast<unsigned char>(c))) {
      allDigits = false;
    }
  }
  return hasDot && !allDigits;
}

}  // namespace UrlUtils
