#include "UrlUtils.h"

#include <HttpText.h>

namespace UrlUtils {
std::string ensureProtocol(const std::string& url) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_url_ensure_protocol_retire_c2(httptransfer::nativeText(url), output, &httptransfer::textLeaves);
  });
}

std::string extractHost(const std::string& url) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_url_extract_host_retire_c2(httptransfer::nativeText(url), output, &httptransfer::textLeaves);
  });
}

std::string encodeUnsafeUrlChars(const std::string& url) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_url_encode_unsafe_retire_c2(httptransfer::nativeText(url), output, &httptransfer::textLeaves);
  });
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  return httptransfer::textResult([&](XpHttpMutableText* output) {
    xp_url_build_retire_c2(httptransfer::nativeText(serverUrl), httptransfer::nativeText(path), output,
                           &httptransfer::textLeaves);
  });
}
}  // namespace UrlUtils
