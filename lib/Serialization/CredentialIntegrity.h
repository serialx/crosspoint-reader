#pragma once

#include <xp_ffi.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace credential_integrity {

// IEEE CRC-32. This detects accidental credential corruption; it is not an
// authentication mechanism and does not make the existing XOR obfuscation
// cryptographically secure.
inline uint32_t crc32(const std::string_view data) {
  return xp_crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

inline bool validate(const std::string_view data, const size_t expectedLength, const uint32_t expectedCrc32) {
  return data.size() == expectedLength && crc32(data) == expectedCrc32;
}

}  // namespace credential_integrity
