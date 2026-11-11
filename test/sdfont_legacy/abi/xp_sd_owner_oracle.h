#pragma once
// Historical native SD compatibility ABI; never included by firmware.
#ifndef XP_APP_TEST
#error "SD oracle ABI is host-only"
#endif
#ifdef __cplusplus
extern "C" {
#endif
int32_t xp_sd_font_load(XpLoadedFonts*, const XpFontFamilyFiles*, uint8_t points, int32_t id, uint8_t mode);
void xp_sd_font_unload(XpLoadedFonts*, int32_t id, uint8_t all);
const char* xp_sd_font_current(const XpLoadedFonts*, uint8_t* size);
int32_t xp_sd_font_id(const XpLoadedFonts*, const char* family, size_t length, uint8_t size);
uint8_t xp_native_font_compat_resolve(void*, int32_t font, uint8_t style);
void xp_native_font_compat_reset(void*, int32_t font);
const uint8_t* xp_native_sd_font_bitmap(const void* font, const void* glyph);
void xp_native_sd_font_view(void*, int32_t font, XpNativeFamily*, XpGlyphSource*);
void xp_native_sd_font_resident_slice(void*, int32_t font, uint8_t style, const uint8_t*, size_t, uint8_t metadata);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Vector compatibility remains; SD policy uses the Rust owner directly.
int32_t xp_native_font_compat_prewarm(void*, int32_t font, uint8_t backend, const char* text, uint8_t styles,
                                      uint8_t metadata, uint8_t kern_lig, uint8_t accumulate);
void xp_native_font_compat_clear(void*, int32_t font, uint8_t backend, uint8_t resident);

uint8_t xp_native_sd_font_load(void*, int32_t font);

#ifdef __cplusplus
}
#endif
