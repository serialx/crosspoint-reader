#include "UrlUtils.h"

#include <HttpText.h>

namespace UrlUtils {
std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_url_build_retire_c2(httptransfer::nativeText(serverUrl), httptransfer::nativeText(path), output,
                           &httptransfer::textLeaves);
  });
}

bool resolveServerPath(std::string& path, const std::string& requestUrl) {
  if (path.empty() || path[0] != '/' || (path.size() > 1 && path[1] == '/')) return false;
  const size_t scheme = requestUrl.find("://");
  if (scheme == std::string::npos) return false;
  const size_t hostEnd = requestUrl.find_first_of("/?#", scheme + 3);
  path.insert(0, requestUrl, 0, hostEnd);
  return true;
}
}  // namespace UrlUtils
