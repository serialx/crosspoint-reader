#pragma once
#include <xp_font_registry.h>

// The front door only borrows this owner; the integration test initializes it.
class GfxRenderer {
  struct Fonts {
    XpLoadedFonts* owner;
    XpLoadedFonts* raw() const { return owner; }
  } fonts;

 public:
  explicit GfxRenderer(XpLoadedFonts* owner) : fonts{owner} {}
  const auto& getFontMap() const { return fonts; }
};
