#pragma once
#include <xp_plugin.h>

#include <cstdint>
#include <string>
#include <vector>

// Plugin folders may live under any of these SD roots; earlier roots win on
// name collisions. /.crosspoint/plugins is the historical home; /plugins and
// /.plugins are friendlier for users copying folders onto the card from a
// computer.
namespace PluginLocations {
inline constexpr auto& ROOTS = xp_plugin_roots;
inline constexpr size_t ROOT_COUNT = sizeof(ROOTS) / sizeof(ROOTS[0]);
inline constexpr const char* INSTALL_MARKER = ".installing";

inline XpPluginSpan span(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }
inline bool isPluginRoot(const std::string& dir) { return xp_plugin_root(span(dir)); }

// A bundle targeting one direct child of a plugin root may update an existing
// same-named plugin in place instead of creating a duplicate in another root.
// Nested and non-plugin bundle destinations keep their configured behavior.
inline bool shouldReuseInstalledBundleDir(const std::string& destDir, const std::string& subdir) {
  return xp_plugin_reuse_dir(span(destDir), span(subdir));
}

enum class DeviceKind { None, Catalog, Background };
enum class PickerAction { None, Catalog, Readme };

inline bool isDiscoverableEntry(const bool quarantined, const bool hasPluginJs, const bool hasDevice,
                                const bool hasManifest) {
  return xp_plugin_discoverable(quarantined, hasPluginJs, hasDevice, hasManifest);
}

inline DeviceKind classifyDeviceManifest(const bool hasBrowseUrl, const bool hasEvents) {
  return static_cast<DeviceKind>(xp_plugin_classify(hasBrowseUrl, hasEvents));
}

// A catalog opens; any other plugin with a README shows it (web-only plugins
// included, so their setup notes are readable on the device).
inline PickerAction pickerAction(const DeviceKind kind, const bool hasReadme) {
  return static_cast<PickerAction>(xp_plugin_picker(static_cast<uint32_t>(kind), hasReadme));
}

// Exactly MAJOR.MINOR.PATCH: three digit-only components, nothing else (no
// sign, spaces, suffix or fourth part), each fitting in 32 bits.
inline bool parseVersion(const std::string& v, uint32_t (&out)[3]) { return xp_plugin_version(span(v), out); }

// Plugin versions are MAJOR.MINOR.PATCH (the catalog contract). True when
// `catalog` is newer than `installed`. An installed copy without a valid
// version is offered the update; a malformed catalog version never is.
inline bool isNewerVersion(const std::string& catalog, const std::string& installed) {
  return xp_plugin_newer(span(catalog), span(installed));
}

// One SD plugin folder, classified by the marker files it carries.
struct Entry {
  std::string name;          // folder name
  std::string dir;           // "<root>/<name>"
  bool hasPluginJs = false;  // browser-side plugin (plugin.js)
  bool hasDevice = false;    // on-device catalog manifest (device.json)
  bool hasManifest = false;  // web UI card metadata (manifest.json)
};

// Returns installed plugins only while the global Plugin System is enabled.
// The physical scan still uses root priority and direct child folders only.
std::vector<Entry> scanPlugins();

// Raw on-disk probes used only for migration/bootstrap UI. These ignore the
// global Plugin System switch and installation quarantine so recovery can still
// detect pre-existing or interrupted plugin directories.
std::vector<Entry> scanPluginsOnDisk();
bool anyPluginInstalledOnDisk();
std::string findPluginDirOnDisk(const char* name);
bool isPluginQuarantined(const std::string& dir);

// Directory of the named plugin ("<root>/<name>"), or "" when absent,
// globally disabled, or quarantined by an in-progress installation.
std::string findPluginDir(const char* name);
}  // namespace PluginLocations
