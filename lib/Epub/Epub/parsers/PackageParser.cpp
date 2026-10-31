#include "PackageParser.h"

#include <Logging.h>
#include <expat.h>

#include "Epub/BookMetadataCache.h"
#include "PackageDiagnostics.h"

PackageParser::~PackageParser() { xp_pkg_pass_destroy(pass); }
bool PackageParser::setup() {
  xp_pkg_pass_destroy(pass);
  pass = nullptr;
  const XpPkgIo itemIo{this, control, read, write, seek, nullptr, nullptr};
  const auto cacheIo = cache ? cache->sink() : XpPkgIo{};
  if (xp_pkg_pass_create(kind, {reinterpret_cast<const uint8_t*>(base.data()), base.size()}, size, metadataOnly,
                         cache && !metadataOnly ? &itemIo : nullptr, cache ? cache->writer() : nullptr,
                         cache && cache->writer() ? &cacheIo : nullptr, &pass) != 0) {
    switch (kind) {
      case XP_PKG_CONTAINER:
        LOG_ERR("CTR", "Couldn't allocate memory for parser");
        break;
      case XP_PKG_OPF_PASS:
        LOG_DBG("COF", "Couldn't allocate memory for parser");
        break;
      case XP_PKG_NAV_PASS:
        LOG_DBG("NAV", "Couldn't allocate memory for parser");
        break;
      case XP_PKG_NCX_PASS:
        LOG_DBG("TOC", "Couldn't allocate memory for parser");
        break;
      default:
        LOG_ERR("PKG", "Could not allocate package pass");
        break;
    }
    return false;
  }
  return true;
}
void PackageParser::logError(const XpPkgError& error) const {
  if (!error.code) return;
  if (error.buffer) {
    switch (kind) {
      case XP_PKG_CONTAINER:
        LOG_DBG("CTR", "Couldn't allocate buffer");
        break;
      case XP_PKG_OPF_PASS:
        LOG_ERR("COF", "Couldn't allocate memory for buffer");
        break;
      case XP_PKG_NAV_PASS:
        LOG_DBG("NAV", "Couldn't allocate memory for buffer");
        break;
      case XP_PKG_NCX_PASS:
        LOG_DBG("TOC", "Couldn't allocate memory for buffer");
        break;
      default:
        break;
    }
    return;
  }
  const char* message = XML_ErrorString(static_cast<XML_Error>(error.code));
  const auto line = static_cast<unsigned long>(error.line);
  switch (kind) {
    case XP_PKG_CONTAINER:
      LOG_ERR("CTR", "Parse error: %s", message);
      break;
    case XP_PKG_OPF_PASS:
      LOG_DBG("COF", "Parse error at line %lu: %s", line, message);
      break;
    case XP_PKG_NAV_PASS:
      LOG_DBG("NAV", "Parse error at line %lu: %s", line, message);
      break;
    case XP_PKG_NCX_PASS:
      LOG_DBG("TOC", "Parse error at line %lu: %s", line, message);
      break;
    default:
      break;
  }
}
size_t PackageParser::write(const uint8_t* bytes, size_t len) {
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  XpPkgError error{};
  const auto count = xp_pkg_pass_write(pass, {bytes, len}, &diagnostics, &error);
  logError(error);
  return count;
}
bool PackageParser::finish() {
  const XpPkgDiagnostics diagnostics{nullptr, packageDiagnostic};
  XpPkgError error{};
  const bool ok = xp_pkg_pass_finish(pass, &diagnostics, &error);
  logError(error);
  return ok;
}
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
    case XP_PKG_OPEN_READ: {
      const bool ok = Storage.openFileForRead("COF", std::string(self.cachePath) + "/.items.bin", file);
      if (!ok) LOG_ERR("COF", "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
      return ok;
    }
    case XP_PKG_OPEN_WRITE: {
      const bool ok = Storage.openFileForWrite("COF", std::string(self.cachePath) + "/.items.bin", file);
      if (!ok) LOG_ERR("COF", "Couldn't open temp items file for writing. This is probably going to be a fatal error.");
      return ok;
    }
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
