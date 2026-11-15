#pragma once
#include "Oracle.h"
class GfxRenderer {};
struct Theme {
  void drawPopup(GfxRenderer&, const char* s) {
#ifdef PLUGIN_TOKEN_LIFETIME
    allocation_probe::endToast();
    oracle::record("popup-bytes %zu %u", strlen(s), oracle::hash(reinterpret_cast<const uint8_t*>(s), strlen(s)));
#endif
    oracle::record("popup %s", s);
  }
};
inline Theme GUI;
