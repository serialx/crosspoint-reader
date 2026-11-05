#pragma once
#include <cstdint>
inline int esp_efuse_mac_get_default(uint8_t* p) {
  const uint8_t key[] = {0x02, 0x11, 0x22, 0x80, 0xfe, 0xff};
  for (unsigned i = 0; i < 6; ++i) p[i] = key[i];
  return 0;
}
