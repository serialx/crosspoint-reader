#pragma once
#include <string>

namespace UrlUtils {

/**
 * Build full URL from server URL and path.
 * If path starts with /, it's an absolute path from the host root.
 * Otherwise, it's relative to the server URL.
 */
std::string buildUrl(const std::string& serverUrl, const std::string& path);

// Adds the request origin to a single-slash server path. Returns true on change.
bool resolveServerPath(std::string& path, const std::string& requestUrl);

}  // namespace UrlUtils
