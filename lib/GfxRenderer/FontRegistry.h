#pragma once
#include <EpdFontFamily.h>
#include <FontTableGeneration.h>
#include <xp_font_registry.h>

#include <utility>

// Native UI clients query the Rust owner. Dereferences copy one descriptor;
// they never expose an address into the registry across a loader or mutation.
class FontRegistry {
  XpLoadedFonts storage;

 public:
  struct Value {
    int first;
    EpdFontFamily second;
  };
  struct FamilyView {
    const FontRegistry* owner;
    int id;
    const EpdGlyph* getGlyph(uint32_t cp, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
      const auto* selected = owner->font(id, style);
      return selected ? selected->getGlyph(cp) : nullptr;
    }
    bool hasCodepoint(uint32_t cp, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
      const auto* selected = owner->font(id, style);
      return selected && selected->hasCodepoint(cp);
    }
  };
  struct Arrow {
    FamilyView second;
    const Arrow* operator->() const { return this; }
  };
  class Iterator {
    const FontRegistry* owner;
    uint32_t index;

   public:
    Iterator(const FontRegistry* owner, uint32_t index) : owner(owner), index(index) {}
    bool operator==(const Iterator& other) const { return owner == other.owner && index == other.index; }
    bool operator!=(const Iterator& other) const { return !(*this == other); }
    Iterator& operator++() {
      ++index;
      return *this;
    }
    Value operator*() const {
      XpRegisteredFont entry{};
      xp_font_registry_query(owner->raw(), XP_FONT_AT, 0, index, &entry);
      return {entry.id, family(entry)};
    }
    Arrow operator->() const { return {{owner, owner->idAt(index)}}; }
  };
  explicit FontRegistry(void* native) { xp_font_registry_init(&storage, &fontTableGeneration, native); }
  ~FontRegistry() { xp_font_registry_drop(&storage); }
  FontRegistry(const FontRegistry&) = delete;
  FontRegistry& operator=(const FontRegistry&) = delete;
  FontRegistry(FontRegistry&&) = delete;
  FontRegistry& operator=(FontRegistry&&) = delete;
  const XpLoadedFonts* raw() const { return &storage; }
  XpLoadedFonts* raw() { return &storage; }
  __attribute__((noinline)) int idAt(uint32_t index) const {
    XpRegisteredFont entry{};
    xp_font_registry_query(raw(), XP_FONT_AT, 0, index, &entry);
    return entry.id;
  }
  static EpdFontFamily family(const XpRegisteredFont& e) {
    return EpdFontFamily(e.points, reinterpret_cast<const EpdFont*>(e.slots[0]),
                         reinterpret_cast<const EpdFont*>(e.slots[1]), reinterpret_cast<const EpdFont*>(e.slots[2]),
                         reinterpret_cast<const EpdFont*>(e.slots[3]));
  }
  uint32_t size() const { return xp_font_registry_query(raw(), XP_FONT_COUNT, 0, 0, nullptr).value; }
  Iterator begin() const { return Iterator(this, 0); }
  Iterator end() const { return Iterator(this, size()); }
  Iterator find(int id) const {
    XpRegisteredFont e{};
    const auto found = xp_font_registry_query(raw(), XP_FONT_BY_ID, id, 0, &e).value;
    return Iterator(this, found ? found - 1 : size());
  }
  bool count(int id) const { return xp_font_registry_query(raw(), XP_FONT_EXISTS, id, 0, nullptr).value != 0; }
  const EpdFont* font(int id, EpdFontFamily::Style style) const {
    return reinterpret_cast<const EpdFont*>(xp_font_registry_query(raw(), XP_FONT_SLOT, id, style, nullptr).slot);
  }
  const EpdFontData* data(int id, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    const auto* selected = font(id, style);
    return selected ? selected->data : nullptr;
  }
  EpdFontFamily at(int id) const {
    XpRegisteredFont out{};
    xp_font_registry_query(raw(), XP_FONT_BY_ID, id, 0, &out);
    return family(out);
  }
  std::pair<Iterator, bool> insert(std::pair<int, EpdFontFamily> value) {
    auto e = value.second.registration(value.first);
    const bool inserted = xp_font_registry_update(raw(), XP_FONT_REGISTER, &e);
    return {find(value.first), inserted};
  }
  void erase(int id) {
    const XpRegisteredFont e{id, {}, 0};
    xp_font_registry_update(raw(), XP_FONT_REMOVE, &e);
  }
  void setFallback(int id, int fallback) {
    const XpRegisteredFont e{id, {}, fallback};
    xp_font_registry_update(raw(), XP_FONT_FALLBACK, &e);
  }
  int fallback(int id) const {
    return static_cast<int32_t>(xp_font_registry_query(raw(), XP_FONT_FALLBACK_ID, id, 0, nullptr).value);
  }
  void clearFallbacks() { xp_font_registry_update(raw(), XP_FONT_CLEAR_FALLBACKS, nullptr); }
};
