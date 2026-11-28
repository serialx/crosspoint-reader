#pragma once

#include <xp_font_registry.h>

#include <cstdint>
#include <string>
#include <string_view>

class GfxRenderer;
class SdCardFont;
struct SdCardFontFamilyInfo;
struct SdCardFontFileInfo;

class SdCardFontManager {
 public:
  SdCardFontManager() = default;
  ~SdCardFontManager() = default;
  SdCardFontManager(const SdCardFontManager&) = delete;
  SdCardFontManager& operator=(const SdCardFontManager&) = delete;

  // Load the family's .cpfont at `pointSize`, or the nearest size it ships if
  // that exact size is not installed. Only one .cpfont file is loaded; other
  // sizes remain on disk. This keeps resident interval + kern/ligature tables to
  // one size's worth of memory. Returns true on success.
  bool loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize);

  // Additively load the .cpfont of `family` at the exact physical `pointSize`
  // (used for size-matched CJK UI fallback alongside the reader-size font).
  // Does not unload anything. If a font of that size is already loaded its id
  // is reused; otherwise register it under fontId. Returns the font id, or 0
  // if the family has no file at that size or loading failed.
  int loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize, int fontId);

  // Additively load the family's .cpfont at exactly `pointSize`, registered under `fontId`
  // (a sized variant of the reader font). Returns false when the size is not installed or
  // loading failed.
  bool loadFamilyVariant(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize, int fontId);

  // Unload and unregister one font loaded by loadFamilyVariant.
  void unloadFont(GfxRenderer& renderer, int fontId);

  // Unload everything, unregister from renderer.
  void unloadAll(GfxRenderer& renderer);

  // Look up the loaded font at pointSize (0 selects the reader size).
  // Returns 0 if the family or size is not loaded.
  int getFontId(std::string_view familyName, uint8_t pointSize = 0) const;

  // Get name of currently loaded family (empty if none).
  std::string_view currentFamilyName() const;

  // Point size that was actually loaded.
  // 0 if nothing loaded.
  uint8_t currentPointSize() const;

 private:
  // Borrowed renderer owner; this cold catalog adapter owns no loaded state.
  XpLoadedFonts* owner_ = nullptr;
};
