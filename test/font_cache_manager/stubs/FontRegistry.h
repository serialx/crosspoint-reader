#pragma once
#include <EpdFontFamily.h>
#include <FontTableGeneration.h>
#include <SdCardFont.h>
#include <xp_font_registry.h>

#include <map>

// Fixture metadata uses the real in-place Rust registry/cache owner.
class FontRegistry : public std::map<int, EpdFontFamily> {
  XpLoadedFonts storage;

 public:
  using Base = std::map<int, EpdFontFamily>;
  FontRegistry() { xp_font_registry_init(&storage, &fontTableGeneration, nullptr); }
  FontRegistry(std::initializer_list<Base::value_type> fonts) : Base(fonts) {
    xp_font_registry_init(&storage, &fontTableGeneration, nullptr);
  }
  void attach(const std::map<int, SdCardFont*>& fonts) {
    for (const auto& [id, font] : fonts)
      Base::insert({id, EpdFontFamily(&font->data, &font->data, &font->data, &font->data)});
    xp_font_registry_init(&storage, &fontTableGeneration, const_cast<void*>(static_cast<const void*>(&fonts)));
    for (const auto& [id, family] : *this) {
      const auto entry = family.registration(id);
      xp_font_registry_update(&storage, XP_FONT_REGISTER, &entry);
    }
  }
  const XpLoadedFonts* raw() const { return &storage; }
};
