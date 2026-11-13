#pragma once
#include <xp_sd_physical.h>

#include <cassert>

// Directory state belongs to the host fake; Rust owns discovery and catalog policy.
namespace catalog_fixture {
struct Directory {
  std::string prefix;
  decltype(host::files)::const_iterator next;
};
struct Handle {
  Directory* value = nullptr;
};
static_assert(sizeof(Handle) <= sizeof(XpSdFileSlot));
static_assert(alignof(Handle) <= alignof(XpSdFileSlot));
inline Directory*& directory(XpSdFileSlot* slot) { return reinterpret_cast<Handle*>(slot)->value; }
}  // namespace catalog_fixture
extern "C" {
void xp_native_sd_handle_init(XpSdFileSlot* slot) { new (slot) catalog_fixture::Handle{}; }
void xp_native_sd_handle_destroy(XpSdFileSlot* slot) {
  delete catalog_fixture::directory(slot);
  catalog_fixture::directory(slot) = nullptr;
}
int32_t xp_native_sd_directory_open(XpSdFileSlot* slot, const char* path) {
  auto found = host::files.find(path);
  if (found == host::files.end()) return 0;
  if (!found->second->directory) return -1;
  delete catalog_fixture::directory(slot);
  catalog_fixture::directory(slot) = new catalog_fixture::Directory{std::string(path) + "/", host::files.begin()};
  return 1;
}
bool xp_native_sd_directory_next(XpSdFileSlot* slot, char* name, bool* directory, bool) {
  auto* state = catalog_fixture::directory(slot);
  assert(state);
  while (state->next != host::files.end()) {
    const auto& [path, node] = *state->next++;
    if (!path.starts_with(state->prefix)) continue;
    auto entry = path.substr(state->prefix.size());
    if (entry.empty() || entry.find('/') != std::string::npos) continue;
    assert(entry.size() < 128);
    std::memcpy(name, entry.c_str(), entry.size() + 1);
    *directory = node->directory;
    return true;
  }
  return false;
}
bool xp_native_sd_path_exists(const char* path) { return host::files.contains(path); }
bool xp_native_sd_inspect(const char*, uint16_t*, bool*) { return false; }
void xp_native_sd_diagnostic(const char*, uint32_t, const uint32_t*, uint32_t, uint32_t, bool, const char*) {}
}
