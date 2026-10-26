#include "ContainerParser.h"

#include <Logging.h>

#include "Epub/BookMetadataCache.h"

bool ContainerParser::setup() {
  xp_pkg_destroy(parser);
  parser = nullptr;
  if (xp_pkg_container_create(nullptr, 0, &parser) != 0) {
    LOG_ERR("PKG", "Could not allocate package parser");
    return false;
  }
  return true;
}
size_t ContainerParser::write(uint8_t byte) { return write(&byte, 1); }
size_t ContainerParser::write(const uint8_t* bytes, size_t size) {
  if (!parser) return 0;
  size_t offset = 0;
  while (offset < size) {
    const size_t count = std::min(size - offset, size_t{1024});
    size_t consumed = 0;
    bool ok = xp_pkg_feed(parser, bytes + offset, count, remainingSize == count, &consumed) == 0;
    while (ok) {
      XpPkgRecord record{};
      if (xp_pkg_next(parser, &record) != 0) {
        ok = false;
        break;
      }
      if (record.kind == XP_PKG_ERROR) {
        ok = false;
        break;
      }
      if (record.kind == XP_PKG_NEED_INPUT || record.kind == XP_PKG_DONE) break;
      if (record.kind == XP_PKG_CONTAINER_PATH) {
        fullPath.assign(reinterpret_cast<const char*>(record.value.data), record.value.len);
      }
    }
    if (!ok || consumed != count) {
      LOG_ERR("PKG", "Package parse failed");
      xp_pkg_destroy(parser);
      parser = nullptr;
      return 0;
    }
    offset += count;
    remainingSize -= count;
  }
  return size;
}
