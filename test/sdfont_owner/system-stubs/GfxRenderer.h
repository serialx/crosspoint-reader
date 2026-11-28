#pragma once
#include <FontRegistry.h>
#include <FontVariantProvider.h>
#include <ReaderFontSizes.h>
class GfxRenderer {
  FontRegistry fonts{this};

 public:
  FontVariantProvider provider;
  auto& getFontMap() { return fonts; }
  const auto& getFontMap() const { return fonts; }
  void setFontVariantProvider(FontVariantProvider value) { provider = value; }
  void setFallbackFont(int primary, int fallback) { fonts.setFallback(primary, fallback); }
  int resolveFontVariant(int id, float scale) const {
    if (std::fabs(scale - 1.0f) < 0.05f) return id;
    if (int builtin = builtinReaderFontVariant(id, scale); builtin) return fonts.count(builtin) ? builtin : 0;
    if (int sd = xp_sd_font_variant(fonts.raw(), id, scale); sd) return sd;
    return provider.resolveFont(id, scale);
  }
};
