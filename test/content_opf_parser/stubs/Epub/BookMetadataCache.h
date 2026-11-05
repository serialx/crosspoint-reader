#pragma once

#include <xp_ffi.h>

#include <string>

class BookMetadataCache {
 public:
  XpPkgCache* writer() const { return nullptr; }
  XpPkgIo sink() const { return {}; }
  void createSpineEntry(const std::string&) {}
};
