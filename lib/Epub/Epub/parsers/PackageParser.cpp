#include "PackageParser.h"

#include <Logging.h>

#include "Epub/BookMetadataCache.h"

PackageParser::~PackageParser() { xp_pkg_pass_destroy(pass); }
bool PackageParser::setup() {
  xp_pkg_pass_destroy(pass);
  pass = nullptr;
  const XpPkgIo itemIo{this, control, read, write, seek, nullptr, nullptr};
  const auto cacheIo = cache ? cache->sink() : XpPkgIo{};
  if (xp_pkg_pass_create(kind, {reinterpret_cast<const uint8_t*>(base.data()), base.size()}, size, metadataOnly,
                         cache && !metadataOnly ? &itemIo : nullptr, cache ? cache->writer() : nullptr,
                         cache && cache->writer() ? &cacheIo : nullptr, &pass) != 0) {
    LOG_ERR("PKG", "Could not allocate package pass");
    return false;
  }
  return true;
}
size_t PackageParser::write(const uint8_t* bytes, size_t len) { return xp_pkg_pass_write(pass, {bytes, len}); }
std::string_view PackageParser::view(uint32_t field) const {
  const auto value = xp_pkg_pass_value(pass, field);
  return {reinterpret_cast<const char*>(value.data), value.len};
}
std::string PackageParser::value(uint32_t field) const { return std::string(view(field)); }
std::optional<float> PackageParser::seriesIndex() const {
  float value;
  if (xp_pkg_pass_index(pass, &value)) return value;
  return std::nullopt;
}
std::vector<std::string> PackageParser::cssFiles() const {
  std::vector<std::string> result;
  const size_t count = xp_pkg_pass_count(pass);
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) result.push_back(value(32 + i));
  return result;
}
uint32_t PackageParser::control(void* context, uint32_t, uint32_t operation) {
  auto& self = *static_cast<PackageParser*>(context);
  auto& file = self.items;
  switch (operation) {
    case XP_PKG_OPEN_READ:
      return Storage.openFileForRead("PKG", std::string(self.cachePath) + "/.items.bin", file);
    case XP_PKG_OPEN_WRITE:
      return Storage.openFileForWrite("PKG", std::string(self.cachePath) + "/.items.bin", file);
    case XP_PKG_CLOSE:
      file.close();
      return 1;
    case XP_PKG_REMOVE: {
      const auto path = std::string(self.cachePath) + "/.items.bin";
      return !Storage.exists(path.c_str()) || Storage.remove(path.c_str());
    }
    case XP_PKG_IS_OPEN:
      return static_cast<bool>(file);
    case XP_PKG_POSITION:
      return file.position();
    case XP_PKG_SIZE:
      return file.size();
    default:
      return 0;
  }
}
size_t PackageParser::read(void* context, uint32_t, uint8_t* bytes, size_t size) {
  const int read = static_cast<PackageParser*>(context)->items.read(bytes, size);
  return read < 0 ? 0 : static_cast<size_t>(read);
}
size_t PackageParser::write(void* context, uint32_t, const uint8_t* bytes, size_t size) {
  return static_cast<PackageParser*>(context)->items.write(bytes, size);
}
uint8_t PackageParser::seek(void* context, uint32_t, uint32_t offset) {
  return static_cast<PackageParser*>(context)->items.seek(offset);
}
