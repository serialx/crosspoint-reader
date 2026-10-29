#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <GlyphBitmap.h>
#include <SdCardFont.h>
#include <Utf8.h>
#include <builtinFonts/all.h>
#include <xp_ffi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <vector>

void referenceGlyph(const GfxRenderer&, const EpdFontFamily&, uint32_t, const EpdGlyph*, int, int, int, int, int, bool);
static bool exhaustive = std::getenv("GLYPH_DIFF_QUICK") == nullptr;
static uint64_t rasterCases = 0, bitmapCases = 0, cullCases = 0, glyphs = 0;
static_assert(sizeof(XpRasterGlyph) == sizeof(EpdGlyph));
static_assert(sizeof(XpGlyphPageEntry) == sizeof(XpFontCacheEntry));
static_assert(sizeof(XpGlyphPageSlot) == sizeof(XpFontCacheSlot));
static_assert(offsetof(XpGlyphPageSlot, count) == offsetof(XpFontCacheSlot, glyphCount));
static_assert(offsetof(XpGlyphPageSlot, entries) == offsetof(XpFontCacheSlot, glyphs));
static_assert(offsetof(XpGlyphPageSlot, font) == offsetof(XpFontCacheSlot, fontData));
static_assert(offsetof(XpGlyphPageEntry, bufferOffset) == offsetof(XpFontCacheEntry, bufferOffset));

static void require(bool value, const char* message) {
  if (!value) {
    std::cerr << message << " raster=" << rasterCases << " bitmap=" << bitmapCases << '\n';
    std::exit(1);
  }
}
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_allocation_attempts();
static uint8_t checkedDraw(const XpGlyphTarget* target, const XpGlyphDraw* draw, const uint8_t* bytes, uint32_t len) {
  xp_test_fail_allocation_after(0);
  const auto result = xp_glyph_draw(target, draw, bytes, len);
  require(xp_test_allocation_attempts() == 0, "Rust glyph draw allocated");
  xp_test_fail_allocation_after(SIZE_MAX);
  return result;
}
static uint8_t checkedBitmap(const XpGlyphSource* source, const void* glyph, XpGlyphBitmap* out) {
  xp_test_fail_allocation_after(0);
  const auto result = xp_glyph_bitmap(source, glyph, out);
  require(xp_test_allocation_attempts() == 0, "Rust bitmap view allocated");
  xp_test_fail_allocation_after(SIZE_MAX);
  return result;
}
static std::string utf8(uint32_t cp) {
  char bytes[5]{};
  utf8AppendCodepoint(bytes, cp);
  return bytes;
}
static uint32_t glyphCount(const EpdFontData* f) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < f->intervalCount; ++i)
    count = std::max(count, f->intervals[i].offset + f->intervals[i].last - f->intervals[i].first + 1);
  return count;
}
static uint32_t bitmapLength(const EpdFontData* f) {
  uint32_t len = 0;
  for (uint32_t i = 0; i < glyphCount(f); ++i) len = std::max(len, f->glyph[i].dataOffset + f->glyph[i].dataLength);
  return len;
}
static XpGlyphSource source(const EpdFontData* f, FontDecompressor& dc, std::atomic<uint32_t>& gen,
                            SdCardFont* sd = nullptr) {
  return {&gen,
          gen.load(),
          f,
          glyphCount(f),
          f->groups ? 0 : bitmapLength(f),
          reinterpret_cast<const XpGlyphPageSlot*>(static_cast<const XpFontCacheSlot*>(dc.pageSlotView())),
          dc.pageSlotViewCount(),
          sd ? &sd->overflow_[0].glyph : nullptr,
          sd ? uint32_t(sizeof(sd->overflow_[0])) : 0,
          sd ? sd->overflowCount_ : 0};
}
static void checkBitmap(GfxRenderer& r, FontDecompressor& dc, const EpdFontData* f, const EpdGlyph* g,
                        std::atomic<uint32_t>& gen, SdCardFont* sd = nullptr) {
  auto view = source(f, dc, gen, sd);
  XpGlyphBitmap got{};
  const bool overflow = sd && sd->isOverflowGlyph(g);
  bool resident = !f->groups || !f->groupCount;
  if (!resident)
    for (uint8_t i = 0; i < dc.pageSlotViewCount(); ++i) {
      const auto& slot = static_cast<const XpFontCacheSlot*>(dc.pageSlotView())[i];
      if (slot.fontData != f || slot.glyphCount == 0) continue;
      for (uint16_t j = 0; j < slot.glyphCount; ++j)
        if (slot.glyphs[j].glyphIndex == uint32_t(g - f->glyph) && slot.glyphs[j].bufferOffset != UINT32_MAX)
          resident = true;
      break;
    }
  const bool hit = checkedBitmap(&view, g, &got);
  const uint8_t* expected = r.getGlyphBitmap(f, g);
  if (overflow || !resident)
    require(!hit, "Nonresident must miss");
  else {
    require(hit, "Resident bitmap missed");
    require(got.data == expected, "Bitmap pointer differs");
    require(got.len == (uint32_t(g->width) * g->height * (f->is2Bit ? 2 : 1) + 7) / 8, "Bitmap length differs");
  }
  XpGlyphSource ownerView{};
  r.glyphSource(f, ownerView);
  XpGlyphBitmap ownerBitmap{};
  require(checkedBitmap(&ownerView, g, &ownerBitmap) == hit, "Owner bitmap view hit differs");
  if (hit) require(ownerBitmap.data == got.data && ownerBitmap.len == got.len, "Owner bitmap view differs");
  ++bitmapCases;
  ++gen;
  // Deliberately poison borrowed pointers: stale rejection must precede reads.
  view.font = reinterpret_cast<void*>(1);
  view.slots = reinterpret_cast<XpGlyphPageSlot*>(1);
  require(!checkedBitmap(&view, reinterpret_cast<void*>(1), &got) && !got.data, "Stale view accessed");
  ++bitmapCases;
}

static void compare(GfxRenderer& r, const EpdFontFamily& family, uint32_t cp, const EpdGlyph* glyph, int kind,
                    int orientation, int plane, bool absolute, bool state, int position, int band, int num = 1,
                    int den = 2, int scale = 384, bool solid = false, bool scanning = false) {
  constexpr int W = 64, H = 48, STRIDE = 8, GUARD = 16;
  constexpr int SIZE = STRIDE * H + 2 * GUARD;
  std::array<uint8_t, SIZE> expected{}, actual{}, inactive{};
  for (int i = 0; i < SIZE; ++i) expected[i] = actual[i] = inactive[i] = uint8_t(i * 53 + 0xa5);
  const auto old = r.frameBuffer;
  r.panelWidth = W;
  r.panelHeight = H;
  r.panelWidthBytes = STRIDE;
  r.frameBufferSize = STRIDE * H;
  r.frameBuffer = expected.data() + GUARD;
  r.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
  r.renderMode = static_cast<GfxRenderer::RenderMode>(plane);
  r.absoluteGrayPlanes = absolute;
  const int sw = r.getScreenWidth(), sh = r.getScreenHeight();
  const std::array<std::pair<int, int>, 9> positions = {{{sw / 2, sh / 2},
                                                         {-3, 12},
                                                         {sw - 2, 12},
                                                         {12, -3},
                                                         {12, sh + 2},
                                                         {8, 12},
                                                         {sw - 10, 12},
                                                         {12, 8},
                                                         {12, sh - 8}}};
  const auto [x, y] = positions[position];
  if (position < 5)
    r.setClipRect(-64, -64, sw + 128, sh + 128);
  else
    r.setClipRect(10, 10, sw - 20, sh - 20);
  const int origin = band == 0 ? 0 : band == 1 ? 0 : 19;
  const int rows = band == 0 ? H : band == 1 ? 7 : 13;
  if (band) {
    r.frameBuffer = inactive.data() + GUARD;
    r.beginStripTarget(expected.data() + GUARD, origin, rows);
  }
  XpGlyphTarget target{actual.data() + GUARD,
                       uint32_t(STRIDE * rows),
                       W,
                       H,
                       STRIDE,
                       origin,
                       rows,
                       {r.clipLeft_, r.clipTop_, r.clipRight_, r.clipBottom_},
                       uint8_t(orientation),
                       uint8_t(plane),
                       uint8_t(absolute),
                       uint8_t(band != 0),
                       uint8_t(scanning)};
  XpGlyphDraw draw{};
  memcpy(&draw.glyph, glyph, sizeof(EpdGlyph));
  draw.x = x;
  draw.y = y;
  draw.ascender = family.getData(EpdFontFamily::REGULAR)->ascender;
  draw.scaleNum = num;
  draw.scaleDen = den;
  draw.scale256 = scale;
  draw.kind = kind;
  draw.twoBit = family.getData(EpdFontFamily::REGULAR)->is2Bit;
  draw.state = state;
  draw.solid = solid;
  const uint8_t* bitmap = solid ? nullptr : r.getGlyphBitmap(family.getData(EpdFontFamily::REGULAR), glyph);
  const uint32_t len = solid ? 0 : (uint32_t(glyph->width) * glyph->height * (draw.twoBit ? 2 : 1) + 7) / 8;
  require(checkedDraw(&target, &draw, bitmap, len), "Rust draw rejected valid input");
  std::optional<FontCacheManager::PrewarmScope> scope;
  if (scanning) scope.emplace(*r.getFontCacheManager());
  if (kind == XP_GLYPH_BILINEAR)
    r.drawScaledGlyph(999, cp, EpdFontFamily::REGULAR, x, y, scale);
  else
    referenceGlyph(r, family, cp, solid ? nullptr : glyph, x, y, kind, num, den, state);
  scope.reset();
  if (expected != actual) {
    std::cerr << "cp=" << cp << " kind=" << kind << " orientation=" << orientation << " plane=" << plane
              << " absolute=" << absolute << " state=" << state << " pos=" << position << " band=" << band
              << " scale=" << scale << '\n';
    for (int i = 0; i < SIZE; ++i)
      if (expected[i] != actual[i]) {
        std::cerr << "byte " << i << " cpp=" << int(expected[i]) << " rust=" << int(actual[i]) << '\n';
        break;
      }
    require(false, "Pixel difference");
  }
  for (int i = 0; i < SIZE; ++i) require(inactive[i] == uint8_t(i * 53 + 0xa5), "Inactive buffer modified");
  for (auto bbox : {std::array<int, 4>{x, y, x + glyph->width - 1, y + glyph->height - 1},
                    std::array<int, 4>{x, y, x - 1, y - 1}}) {
    require(bool(xp_glyph_intersects_strip(&target, bbox[0], bbox[1], bbox[2], bbox[3])) ==
                r.glyphIntersectsStrip(bbox[0], bbox[1], bbox[2], bbox[3]),
            "Cull difference");
    ++cullCases;
  }
  r.endStripTarget();
  r.frameBuffer = old;
  ++rasterCases;
}
static void matrix(GfxRenderer& r, const EpdFontFamily& family, uint32_t cp, const EpdGlyph* g, bool exhaustive) {
  r.removeFont(999);
  r.insertFont(999, family);
  for (int orientation = 0; orientation < 4; ++orientation)
    for (int plane = 0; plane < 3; ++plane)
      for (int state = 0; state < 2; ++state)
        for (int kind = 0; kind < 2; ++kind) {
          if (exhaustive) {
            for (int absolute = 0; absolute < 2; ++absolute)
              for (int band = 0; band < 3; ++band)
                for (int pos = 0; pos < 9; ++pos)
                  compare(r, family, cp, g, kind, orientation, plane, absolute, state, pos, band);
          } else {
            const int variant = int(cp + orientation + plane + state + kind);
            compare(r, family, cp, g, kind, orientation, plane, variant % 2, state, variant % 9, variant % 3);
          }
        }
  // Every glyph uses both reduction algorithms and bilinear scaling in every orientation.
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (auto ratio : {std::pair{1, 2}, std::pair{3, 4}})
      compare(r, family, cp, g, XP_GLYPH_COVERAGE, orientation, 2, true, false, 0, 0, ratio.first, ratio.second);
    compare(r, family, cp, g, XP_GLYPH_BILINEAR, orientation, 0, false, true, 0, 0, 1, 1, 384);
  }
}
static void panelMatrix(GfxRenderer& r) {
  const auto saved = r.frameBuffer;
  std::array<uint8_t, 102> bitmap{};
  for (size_t i = 0; i < bitmap.size(); ++i) bitmap[i] = uint8_t(i * 73 + 0x1b);
  for (auto dimensions : {std::array<int, 3>{40, 32, 5}, {63, 47, 9}, {800, 480, 100}, {792, 528, 99}}) {
    const auto [width, height, stride] = dimensions;
    r.panelWidth = width;
    r.panelHeight = height;
    r.panelWidthBytes = stride;
    std::vector<uint8_t> expected(stride * height + 32), actual(expected.size());
    for (int o = 0; o < 4; ++o)
      for (int rotation = 0; rotation < 2; ++rotation)
        for (int plane = 0; plane < 3; ++plane)
          for (bool state : {false, true})
            for (bool absolute : {false, true})
              for (int band = 0; band < 3; ++band)
                for (int edge = 0; edge < 5; ++edge) {
                  for (size_t i = 0; i < expected.size(); ++i) expected[i] = actual[i] = uint8_t(i * 53 + 0xa5);
                  r.frameBuffer = expected.data() + 16;
                  r.setOrientation(static_cast<GfxRenderer::Orientation>(o));
                  r.setClipRect(3, 4, r.getScreenWidth() - 6, r.getScreenHeight() - 8);
                  const int origin = band == 0 ? 0 : band == 1 ? 0 : height - 7;
                  const int rows = band == 0 ? height : 7;
                  if (band) r.beginStripTarget(expected.data() + 16, origin, rows);
                  XpGlyphTarget t{actual.data() + 16,
                                  uint32_t(stride * rows),
                                  width,
                                  height,
                                  stride,
                                  origin,
                                  rows,
                                  {r.clipLeft_, r.clipTop_, r.clipRight_, r.clipBottom_},
                                  uint8_t(o),
                                  uint8_t(plane),
                                  uint8_t(absolute),
                                  uint8_t(band != 0),
                                  0};
                  const std::array<std::pair<int, int>, 5> positions = {
                      {{20, 20}, {-3, 20}, {r.getScreenWidth() - 2, 20}, {20, -3}, {20, r.getScreenHeight() + 2}}};
                  const auto [x, y] = positions[edge];
                  XpGlyphDraw d{
                      {31, 13, 16, -3, 9, 102, 0}, x, y, 13, 1, 1, 256, uint8_t(rotation), 1, uint8_t(state), 0};
                  glyphBitmap::Frame f = rotation ? glyphBitmap::Frame{x + 4, y + 3, 0, -1, 1, 0}
                                                  : glyphBitmap::Frame{x - 3, y - 9, 1, 0, 0, 1};
                  require(checkedDraw(&t, &d, bitmap.data(), bitmap.size()), "Panel draw rejected");
                  r.drawGlyphBitmap(bitmap.data(), 31, 13, f, true,
                                    absolute ? GfxRenderer::BW : static_cast<GfxRenderer::RenderMode>(plane), state);
                  require(expected == actual, "Panel/stride pixel difference");
                  r.endStripTarget();
                  ++rasterCases;
                }
  }
  r.frameBuffer = saved;
}

// A miss may free the old tables. The next placement must use the new slot,
// while the just-returned bitmap and copied metrics remain consumable.
static void integrationLifetime(GfxRenderer& renderer) {
  std::vector<EpdGlyph> old(3);
  std::array<EpdGlyph, 3> next{};
  const std::array<uint8_t, 3> bits{0x80, 0x80, 0x80};
  const EpdUnicodeInterval interval{'A', 'C', 0};
  for (size_t i = 0; i < 3; ++i) {
    old[i] = {1, 1, static_cast<uint16_t>((i + 1) * 16), 0, 0, 1, static_cast<uint32_t>(i)};
    next[i] = {1, 1, static_cast<uint16_t>((i + 1) * 160), 0, 0, 1, static_cast<uint32_t>(i)};
  }
  EpdFontData before{}, after{};
  before.glyph = old.data();
  after.glyph = next.data();
  before.bitmap = after.bitmap = bits.data();
  before.intervals = after.intervals = &interval;
  before.intervalCount = after.intervalCount = 1;
  EpdFont font(&before);
  renderer.insertFont(998, EpdFontFamily(&font));
  std::array<uint8_t, 64> buffer;
  buffer.fill(0xff);
  XpGlyphPaint paint{{buffer.data(), 64, 32, 16, 4, 0, 16, {0, 0, 32, 16}, 3, 0, 0, 0, 0}, {}, 1, 1, 1};
  struct State {
    GfxRenderer& renderer;
    EpdFont& font;
    EpdFontData& after;
    std::vector<EpdGlyph>& old;
    const uint8_t* bits;
    XpGlyphPaint& paint;
    unsigned misses = 0, observed = 0;
  } state{renderer, font, after, old, bits.data(), paint};
  const auto provider = renderer.textProvider();
  xp_test_fail_allocation_after(0);
  xp_native_draw_pixels_traced(
      &provider, 998, reinterpret_cast<const uint8_t*>("ABC"), 3, 0, 0, 0, 0, &state,
      [](void* raw, const uint8_t*, size_t) -> const void* const* {
        auto& s = *static_cast<State*>(raw);
        s.renderer.glyphSource(s.font.data, s.paint.source);
        ++fontTableGeneration;
        s.paint.source.font = reinterpret_cast<const void*>(1);
        s.paint.source.slots = reinterpret_cast<const XpGlyphPageSlot*>(1);
        return reinterpret_cast<const void* const*>(&s.font.data);
      },
      &paint,
      [](void* raw, uint32_t cp, const void* ptr, XpGlyphDraw* draw, XpGlyphSource* source) -> XpGlyphBitmap {
        auto& s = *static_cast<State*>(raw);
        require(cp == 'A' && ptr == s.old.data() && s.misses++ == 0, "Unexpected integrated miss");
        memcpy(&draw->glyph, ptr, sizeof(EpdGlyph));
        s.font.data = &s.after;
        ++fontTableGeneration;
        std::vector<EpdGlyph>().swap(s.old);
        s.renderer.glyphSource(s.font.data, *source);
        return {s.bits, 1};
      },
      [](void* raw, const XpGlyphPlacement* placement) {
        auto& s = *static_cast<State*>(raw);
        constexpr int positions[]{0, 1, 21};
        require(s.observed < 3 && placement->x == positions[s.observed++], "Stale integrated placement");
      });
  require(xp_test_allocation_attempts() == 0, "Integrated draw allocated");
  xp_test_fail_allocation_after(SIZE_MAX);
  require(state.misses == 1 && state.observed == 3, "Integrated callback count");
  for (int x = 0; x < 32; ++x) {
    const bool black = (buffer[4 + x / 8] & (0x80 >> (x % 8))) == 0;
    require(black == (x == 1 || x == 2 || x == 22), "Integrated pixels after table replacement");
  }
  renderer.removeFont(998);
  std::cout << "Integrated lifetime: stale poisoned view, table free/replacement, one miss, zero allocations passed\n";
}

int main(int argc, char** argv) {
  HalDisplay display;
  GfxRenderer r(display);
  r.begin();
  FontDecompressor dc;
  require(dc.init(), "decompressor init");
  FontCacheManager cache(r.getFontMap(), r.getSdCardFonts(), r.getTtfFonts());
  cache.setFontDecompressor(&dc);
  r.setFontCacheManager(&cache);
  integrationLifetime(r);
  std::atomic<uint32_t> gen{1};
  const EpdFontData* faces[] = {
#define FACE(S) &notosans_##S##_regular, &notosans_##S##_bold, &notosans_##S##_italic, &notosans_##S##_bolditalic,
      FACE(12) FACE(14) FACE(16) FACE(18)
#undef FACE
          & ubuntu_8_regular,
      &ubuntu_10_regular, &ubuntu_10_bold, &ubuntu_12_regular, &ubuntu_12_bold};
  for (const auto* f : faces) {
    EpdFont font(f);
    EpdFontFamily family(&font);
    std::vector<uint32_t> cps;
    for (uint32_t iv = 0; iv < f->intervalCount; ++iv)
      for (uint32_t cp = f->intervals[iv].first; cp <= f->intervals[iv].last; ++cp) cps.push_back(cp);
    for (size_t first = 0; first < cps.size(); first += 128) {
      dc.clearCache();
      ++gen;
      std::string text;
      for (size_t i = first; i < std::min(first + 128, cps.size()); ++i) text += utf8(cps[i]);
      require(dc.prewarmCache(f, text.c_str()) == 0, "prewarm failed");
      ++gen;
      for (size_t i = first; i < std::min(first + 128, cps.size()); ++i) {
        const auto* g = font.getGlyph(cps[i]);
        require(g != nullptr, "glyph missing");
        checkBitmap(r, dc, f, g, gen);
        matrix(r, family, cps[i], g, exhaustive);
        ++glyphs;
      }
    }
    std::cout << "face glyphs=" << cps.size() << " raster=" << rasterCases << '\n';
  }
  for (int a = 1; a < argc; ++a) {
    SdCardFont sd;
    require(sd.load(argv[a]), "SD load");
    for (uint8_t style = 0; style < 4; ++style) {
      if (!sd.hasStyle(style)) continue;
      const auto& full = sd.styleData(style);
      std::vector<uint32_t> cps;
      for (uint32_t iv = 0; iv < full.header.intervalCount; ++iv) {
        const uint32_t first = full.intervalsAreBmp16 ? full.bmpIntervals[iv].first : full.fullIntervals[iv].first;
        const uint32_t last = full.intervalsAreBmp16 ? full.bmpIntervals[iv].last : full.fullIntervals[iv].last;
        for (uint32_t cp = first; cp <= last; ++cp) cps.push_back(cp);
      }
      for (size_t first = 0; first < cps.size(); first += 128) {
        std::string text;
        for (size_t i = first; i < std::min(first + 128, cps.size()); ++i) text += utf8(cps[i]);
        require(sd.prewarm(text.c_str(), 1 << style, false, true, false) >= 0, "SD prewarm");
        ++gen;
        EpdFont* font = sd.getEpdFont(style);
        EpdFontFamily family(font);
        for (size_t i = first; i < std::min(first + 128, cps.size()); ++i) {
          const auto* g = font->getGlyph(cps[i]);
          if (!g) {
            auto view = source(font->data, dc, gen, &sd);
            XpGlyphBitmap bitmap{};
            require(!checkedBitmap(&view, nullptr, &bitmap), "Missing glyph must miss");
            ++bitmapCases;
            continue;
          }
          checkBitmap(r, dc, font->data, g, gen, &sd);
          matrix(r, family, cps[i], g, exhaustive);
          ++glyphs;
        }
      }
      sd.clearCache();
      ++gen;
      const auto* font = sd.getEpdFont(style);
      const auto* g = font->getGlyph(cps[cps.size() / 2]);
      require(sd.isOverflowGlyph(g), "Expected overflow");
      checkBitmap(r, dc, font->data, g, gen, &sd);
    }
    std::cout << "SD " << argv[a] << " raster=" << rasterCases << '\n';
  }
  // Synthetic packing/scale/empty/solid cases do not depend on a font's coverage.
  std::array<uint8_t, 2048> pixels{};
  for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i * 73 + 0x1b);
  EpdGlyph g{31, 13, 128, -3, 9, 202, 0};
  EpdUnicodeInterval iv{'A', 'A', 0};
  EpdFontData f{};
  f.bitmap = pixels.data();
  f.glyph = &g;
  f.intervals = &iv;
  f.intervalCount = 1;
  f.ascender = 13;
  EpdFont font(&f);
  EpdFontFamily family(&font);
  r.removeFont(999);
  r.insertFont(999, family);
  for (bool two : {false, true}) {
    f.is2Bit = two;
    for (int width : {0, 1, 3, 7, 16, 31}) {
      g.width = width;
      matrix(r, family, 'A', &g, true);
    }
    g.width = 31;
    for (int kind : {XP_GLYPH_COVERAGE, XP_GLYPH_BILINEAR})
      for (int o = 0; o < 4; ++o)
        for (int p = 0; p < 3; ++p)
          for (bool state : {false, true})
            for (bool absolute : {false, true})
              for (int band = 0; band < 3; ++band)
                for (int pos = 0; pos < 9; ++pos) {
                  if (kind == XP_GLYPH_COVERAGE)
                    for (auto ratio : {std::pair{1, 2}, std::pair{3, 4}, std::pair{1, 1}})
                      compare(r, family, 'A', &g, kind, o, p, absolute, state, pos, band, ratio.first, ratio.second);
                  else
                    for (int scale : {64, 128, 192, 255, 256, 257, 384, 512, 768, 1024, 4096})
                      compare(r, family, 'A', &g, kind, o, p, absolute, state, pos, band, 1, 1, scale);
                }
  }
  for (uint32_t cp : {0x2588u, 0x25a0u}) {
    EpdGlyph solid{};
    require(font.getGlyphMetrics(cp, solid) == &solid, "Expected solid fallback");
    for (int kind = 0; kind < 3; ++kind)
      for (int o = 0; o < 4; ++o)
        for (int p = 0; p < 3; ++p)
          for (bool state : {false, true})
            for (bool absolute : {false, true})
              for (int band = 0; band < 3; ++band)
                for (int pos = 0; pos < 9; ++pos)
                  for (bool scanning : {false, true})
                    compare(r, family, cp, &solid, kind, o, p, absolute, state, pos, band, 1, 2, 384, true, scanning);
  }
  panelMatrix(r);
  std::cout << "PASS glyphs=" << glyphs << " raster=" << rasterCases << " bitmap=" << bitmapCases
            << " cull=" << cullCases << '\n';
}
