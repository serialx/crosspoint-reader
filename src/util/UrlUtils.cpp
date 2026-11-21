#include "UrlUtils.h"

#include <HttpText.h>

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
}  // namespace UrlUtils
