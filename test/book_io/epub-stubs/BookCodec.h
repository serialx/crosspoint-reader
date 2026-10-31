#pragma once
#include <HalStorage.h>

#include <cstdlib>
inline bool bookCodec(Print& out, uint8_t kind, bool thumb, int width, int height, bool cropped, bool original) {
  printf("codec %u %u %d %d %u %u\n", kind, thumb, width, height, cropped, original);
  const uint8_t bytes[]{'B', 'M', kind, static_cast<uint8_t>(thumb)};
  return out.write(bytes, sizeof(bytes)) == sizeof(bytes) && !std::getenv("BOOK_CODEC_FAIL");
}
