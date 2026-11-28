#pragma once
#include <FontRegistry.h>
#include <FontVariantProvider.h>
#include <ReaderFontSizes.h>
#include <TtfEpdFont.h>
#include <fontIds.h>

#include <array>
#include <cassert>
class GfxRenderer {
  FontRegistry fonts{this};

 public:
  GfxRenderer() {
    static const EpdFontData data{};
    static const EpdFont builtin(&data);
    for (int id : {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID}) fonts.insert({id, EpdFontFamily(&builtin)});
  }
  FontVariantProvider provider;
  std::array<std::pair<int, TtfEpdFont*>, 32> native{};
  auto& getFontMap() { return fonts; }
  const auto& getFontMap() const { return fonts; }
  void setFontVariantProvider(FontVariantProvider value) { provider = value; }
  void setFallbackFont(int id, int fallback) { fonts.setFallback(id, fallback); }
  void clearFallbackFonts() { fonts.clearFallbacks(); }
  void insertFont(int id, EpdFontFamily family) { assert(fonts.insert({id, family}).second); }
  void removeFont(int id) { fonts.erase(id); }
  void registerTtfFont(int id, TtfEpdFont* font) {
    for (auto& slot : native)
      if (!slot.second) {
        slot = {id, font};
        return;
      }
    assert(false);
  }
  void unregisterTtfFont(int id) {
    for (auto& slot : native)
      if (slot.first == id) slot = {};
  }
  TtfEpdFont* ttf(int id) {
    for (auto& slot : native)
      if (slot.first == id) return slot.second;
    return nullptr;
  }
  int resolveFontVariant(int id, float scale) const {
    if (std::fabs(scale - 1.0f) < 0.05f) return id;
    if (int builtin = builtinReaderFontVariant(id, scale); builtin) return fonts.count(builtin) ? builtin : 0;
    if (int sd = xp_sd_font_variant(fonts.raw(), id, scale); sd) return sd;
    return provider.resolveFont(id, scale);
  }
};
