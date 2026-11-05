#pragma once

#include <array>
#include <vector>

// Emission may evict the consumed bitmap tables. The next query must reread
// the selected EpdFont slot, while the previous advance remains a copied value.
void nativeDrawing(GfxRenderer& renderer) {
  std::vector<EpdGlyph> oldGlyphs(3);
  std::array<EpdGlyph, 3> newGlyphs{};
  const EpdUnicodeInterval interval{65, 67, 0};
  for (size_t i = 0; i < 3; ++i) {
    oldGlyphs[i].advanceX = static_cast<uint16_t>((i + 1) * 16);
    newGlyphs[i].advanceX = static_cast<uint16_t>((i + 1) * 160);
  }
  EpdFontData oldData{}, newData{};
  oldData.glyph = oldGlyphs.data();
  newData.glyph = newGlyphs.data();
  oldData.intervals = newData.intervals = &interval;
  oldData.intervalCount = newData.intervalCount = 1;
  EpdFont font(&oldData);
  renderer.insertFont(3, EpdFontFamily(&font));
  struct State {
    EpdFont& font;
    EpdFontData& next;
    std::vector<EpdGlyph>& old;
    unsigned prepare = 0, emit = 0;
  } state{font, newData, oldGlyphs};
  auto provider = renderer.textProvider();
  provider.family = [](void*, int32_t, XpNativeFamily*) { require(false, "drawing built a family descriptor"); };
  xp_native_draw(
      &provider, 3, reinterpret_cast<const uint8_t*>("ABC"), 3, 0, 0, 0, false, &state,
      [](void* raw, const uint8_t*, size_t) -> const void* const* {
        auto& s = *static_cast<State*>(raw);
        ++s.prepare;
        return reinterpret_cast<const void* const*>(&s.font.data);
      },
      [](void* raw, const XpGlyphPlacement* placement) {
        auto& s = *static_cast<State*>(raw);
        constexpr int positions[]{0, 1, 21};
        require(s.emit < 3 && placement->x == positions[s.emit], "drawing retained stale metrics");
        require(placement->codepoint == 65 + s.emit && placement->glyph, "drawing lost its resident glyph");
        const auto* glyph = static_cast<const EpdGlyph*>(placement->glyph);
        require(glyph->advanceX == (s.emit ? (s.emit + 1) * 160 : 16), "drawing emitted a stale pointer");
        if (s.emit++ == 0) {
          s.font.data = &s.next;
          std::vector<EpdGlyph>().swap(s.old);
        }
      });
  require(state.prepare == 1 && state.emit == 3, "drawing callback count");
  renderer.removeFont(3);
  std::puts("native drawing: slot replacement/free passed; one prepare, three emissions, zero descriptors");
}
