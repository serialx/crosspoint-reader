#pragma once

#include <string.h>
#include <strings.h>
#include <xp_ffi.h>

#include <string>

// Credential stores no web request (file manager, plugin endpoints) or plugin
// manifest may read or write: their passwords are obfuscated with a device
// key, so the files must stay on the device. The prefix list in
// rust/crates/xp-ffi/src/web_jobs/paths.rs follows WifiCredentialStore,
// OpdsServerStore and KOReaderCredentialStore, including temp/backup siblings.
namespace protectedpaths {

// SdFat also opens a name by its generated 8.3 alias (".crosspoint" is
// CROSSP~1), which no spelling check can map back to the long name.
inline bool isShortAlias(const char* first, const char* last) {
  return xp_short_alias(reinterpret_cast<const uint8_t*>(first), static_cast<size_t>(last - first));
}

// Keep the allocation wrapper shared across path-validation callers.
[[gnu::noinline]] inline bool isSensitivePath(const char* path) {
  const size_t len = strlen(path);
  std::string scratch(len + 1, '\0');
  return xp_sensitive_path(reinterpret_cast<const uint8_t*>(path), len, reinterpret_cast<uint8_t*>(scratch.data()));
}

// A plugin-supplied path (web request or device.json): absolute, no parent
// refs, not a credential store.
inline bool isPluginPath(const std::string& p) {
  return xp_plugin_path_candidate(reinterpret_cast<const uint8_t*>(p.data()), p.size()) && !isSensitivePath(p.c_str());
}

}  // namespace protectedpaths
