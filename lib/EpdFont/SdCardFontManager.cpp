#include "SdCardFontManager.h"

#include <GfxRenderer.h>
#include <SdCardFontRegistry.h>

namespace {
XpFontFamilyFiles view(const SdCardFontFamilyInfo& family) {
  return {family.name.c_str(), family.name.size(), family.root, sdFontFileView(family.files), family.loose};
}
}  // namespace
bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t size) {
  owner_ = const_cast<XpLoadedFonts*>(renderer.getFontMap().raw());
  const auto files = view(family);
  return xp_sd_font_load(owner_, &files, size, 0, 0) != 0;
}
int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t size,
                                           int id) {
  owner_ = const_cast<XpLoadedFonts*>(renderer.getFontMap().raw());
  const auto files = view(family);
  return xp_sd_font_load(owner_, &files, size, id, 1);
}
bool SdCardFontManager::loadFamilyVariant(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t size,
                                          int id) {
  owner_ = const_cast<XpLoadedFonts*>(renderer.getFontMap().raw());
  const auto files = view(family);
  return xp_sd_font_load(owner_, &files, size, id, 2) != 0;
}
void SdCardFontManager::unloadFont(GfxRenderer&, int id) {
  if (owner_) xp_sd_font_unload(owner_, id, false);
}
void SdCardFontManager::unloadAll(GfxRenderer&) {
  if (owner_) xp_sd_font_unload(owner_, 0, true);
}
int SdCardFontManager::getFontId(std::string_view family, uint8_t size) const {
  return owner_ ? xp_sd_font_id(owner_, family.data(), family.size(), size) : 0;
}
std::string_view SdCardFontManager::currentFamilyName() const {
  uint8_t size;
  return owner_ ? xp_sd_font_current(owner_, &size) : "";
}
uint8_t SdCardFontManager::currentPointSize() const {
  uint8_t size = 0;
  if (owner_) xp_sd_font_current(owner_, &size);
  return size;
}
