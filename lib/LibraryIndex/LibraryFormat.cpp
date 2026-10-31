#include "LibraryFormat.h"

#include <xp_ffi.h>

namespace library {
uint64_t clixPathHash(const char* data, size_t len) {
  return xp_library_hash(reinterpret_cast<const uint8_t*>(data), len);
}
uint32_t alignUp(uint32_t v) { return xp_library_align(v); }
void layoutSections(ClixHeader& h, uint32_t folders, uint32_t names) { xp_library_layout(&h, folders, names); }
uint32_t recordOffset(const ClixHeader& h, uint16_t n) { return xp_library_offset(&h, n, 0); }
uint32_t authorOrderOffset(const ClixHeader& h, uint16_t n) { return xp_library_offset(&h, n, 1); }
uint32_t arrivalOrderOffset(const ClixHeader& h, uint16_t n) { return xp_library_offset(&h, n, 2); }
ClixValidity validateHeaderStructure(const ClixHeader& h, uint64_t n) {
  return static_cast<ClixValidity>(xp_library_validate(&h, n, 1));
}
ClixValidity validateHeader(const ClixHeader& h, uint64_t n) {
  return static_cast<ClixValidity>(xp_library_validate(&h, n, 0));
}

const char* clixValidityName(const ClixValidity v) {
  switch (v) {
    case ClixValidity::Ok:
      return "ok";
    case ClixValidity::BadMagic:
      return "bad magic";
    case ClixValidity::UnknownFormatVersion:
      return "unknown format version";
    case ClixValidity::StaleFoldVersion:
      return "stale fold version";
    case ClixValidity::SizeMismatch:
      return "size mismatch (truncated?)";
    case ClixValidity::CountOutOfRange:
      return "book count out of range";
    case ClixValidity::SectionsInconsistent:
      return "section offsets inconsistent";
  }
  return "unknown";
}

}  // namespace library
