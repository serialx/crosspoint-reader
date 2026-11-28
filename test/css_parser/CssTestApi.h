#pragma once
#include <xp_ffi.h>
extern "C" {
int32_t xp_css_test_cache_write_begin(XpCss*, uint8_t, uint16_t*, size_t);
int32_t xp_css_test_cache_write(XpCss*, const uint16_t*, size_t, uint8_t*, size_t, size_t*);
}
