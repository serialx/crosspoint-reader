#include "PluginLocations.h"

#include <HalStorage.h>

#include <algorithm>

#include "CrossPointSettings.h"

namespace PluginLocations {
namespace {
struct Scan {
  std::vector<Entry> entries;
  std::vector<std::string> seen;
  HalFile root, entry;
  char name[128]{};
  Entry current;
  XpPluginLocationOps api() {
    return {this,
            [](void* p, size_t r) {
              auto& c = *static_cast<Scan*>(p);
              c.entry = HalFile{};
              c.root = HalFile{};
              c.root = Storage.open(ROOTS[r]);
              return c.root && c.root.isDirectory();
            },
            [](void* p) {
              auto& c = *static_cast<Scan*>(p);
              c.current = Entry{};
              c.entry = c.root.openNextFile();
              return static_cast<bool>(c.entry);
            },
            [](void* p) { return static_cast<Scan*>(p)->entry.isDirectory(); },
            [](void* p) {
              auto& c = *static_cast<Scan*>(p);
              const auto n = c.entry.getName(c.name, sizeof(c.name));
              return XpPluginSpan{reinterpret_cast<const uint8_t*>(c.name), n ? strlen(c.name) : 0};
            },
            [](void* p) {
              auto& c = *static_cast<Scan*>(p);
              return std::find(c.seen.begin(), c.seen.end(), c.name) != c.seen.end();
            },
            [](void* p) {
              auto& c = *static_cast<Scan*>(p);
              const char* const name = c.name;
              c.seen.emplace_back(name);
              c.current.name = c.name;
            },
            [](void* p, size_t r) {
              auto& c = *static_cast<Scan*>(p);
              c.current.dir = std::string(ROOTS[r]) + "/" + c.name;
            },
            [](void* p, const char* marker) {
              return Storage.exists((static_cast<Scan*>(p)->current.dir + marker).c_str());
            },
            [](void* p, bool js, bool device, bool manifest) {
              auto& c = *static_cast<Scan*>(p);
              c.current.hasPluginJs = js;
              c.current.hasDevice = device;
              c.current.hasManifest = manifest;
              c.entries.push_back(std::move(c.current));
            }};
  }
};
std::string find(const char* name, bool enabled, bool quarantine) {
  struct Context {
    const char* name;
    std::string path;
  } context{name, {}};
  static const XpPluginFindOps opsCallbacks{nullptr,
                                            [](void* p, size_t r) {
                                              auto& c = *static_cast<Context*>(p);
                                              std::string{}.swap(c.path);
                                              c.path = std::string(ROOTS[r]) + "/" + c.name;
                                              return Storage.exists(c.path.c_str());
                                            },
                                            [](void* p) { return isPluginQuarantined(static_cast<Context*>(p)->path); },
                                            [](void* p) { static_cast<Context*>(p)->path.clear(); }};
  auto ops = opsCallbacks;
  ops.ctx = &context;
  xp_plugin_find(&ops, enabled, quarantine);
  return std::move(context.path);
}
}  // namespace
std::vector<Entry> scanPluginsOnDisk() {
  Scan scan;
  scan.entries.reserve(8);
  scan.seen.reserve(8);
  const auto ops = scan.api();
  xp_plugin_scan(&ops, false);
  return std::move(scan.entries);
}
std::vector<Entry> scanPlugins() {
  if (!SETTINGS.pluginsEnabled) return {};
  auto plugins = scanPluginsOnDisk();
  plugins.erase(std::remove_if(plugins.begin(), plugins.end(),
                               [](const Entry& e) {
                                 return !isDiscoverableEntry(isPluginQuarantined(e.dir), e.hasPluginJs, e.hasDevice,
                                                             e.hasManifest);
                               }),
                plugins.end());
  return plugins;
}
bool anyPluginInstalledOnDisk() {
  Scan scan;
  const auto ops = scan.api();
  return xp_plugin_scan(&ops, true);
}
std::string findPluginDirOnDisk(const char* name) { return find(name, true, false); }
bool isPluginQuarantined(const std::string& dir) { return Storage.exists((dir + "/" + INSTALL_MARKER).c_str()); }
std::string findPluginDir(const char* name) { return find(name, SETTINGS.pluginsEnabled, true); }
}  // namespace PluginLocations
