#include <FontTableGeneration.h>
#include <TtfEpdFont.h>
#include <builtinFonts/all.h>
#include <xp_ffi.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "FrozenRenderer.h"

extern "C" void xp_native_test_run(const XpNativeProvider*, void*, void (*)(void*, const void*));
extern "C" int32_t xp_native_test_advance(const void*, int32_t, const uint8_t*, size_t, uint8_t);
namespace {
void require(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "%s\n", why);
    std::exit(1);
  }
}
struct Fixture;
Fixture* activeFixture = nullptr;
struct Fixture {
  HalDisplay display;
  GfxRenderer renderer{display};
  SdCardFont sd;
  std::vector<uint8_t> ttfBytes;
  TtfEpdFont ttf;
  FrozenRenderer baseline;
  XpNativeProvider raw{};
  unsigned setups = 0, families = 0, evictions = 0, sdAfterEviction = 0;
  bool evict = false;
  const EpdGlyph* (*originalMiss)(void*, uint32_t) = nullptr;

  Fixture(const char* sdPath, const char* ttfPath, bool forceEviction) : evict(forceEviction) {
    require(activeFixture == nullptr, "fixtures must execute serially");
    activeFixture = this;
    static EpdFont regular(&notosans_12_regular), bold(&notosans_12_bold), other(&ubuntu_12_regular);
    renderer.insertFont(1, EpdFontFamily(&regular, &bold));
    renderer.insertFont(4, EpdFontFamily(&other));
    require(sd.load(sdPath), "load SD font");
    renderer.insertFont(2, EpdFontFamily(sd.getEpdFont(0), sd.getEpdFont(1), sd.getEpdFont(2), sd.getEpdFont(3)));
    renderer.registerSdCardFont(2, &sd);
    std::ifstream input(ttfPath, std::ios::binary);
    require(bool(input), "open TTF");
    ttfBytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    ttf.addResidentSource(0, ttfBytes.data(), ttfBytes.size());
    require(ttf.load(14), "load TTF");
    auto family = ttf.family();
    // Inject memory pressure inside the real TTF glyph callback on both backends.
    auto* data = const_cast<EpdFontData*>(family.getData(EpdFontFamily::REGULAR));
    originalMiss = data->glyphMissHandler;
    require(originalMiss != nullptr, "TTF miss handler");
    data->glyphMissHandler = [](void* context, uint32_t cp) -> const EpdGlyph* {
      auto& f = *activeFixture;
      if (f.evict && f.evictions == 0) {
        require(f.sd.hasAdvanceTable(), "eviction must release a prepared SD cache");
        f.sd.releaseResidentCaches();
        require(!f.sd.hasAdvanceTable(), "resident cache was not released");
        ++f.evictions;
      }
      return f.originalMiss(context, cp);
    };
    renderer.insertFont(3, family);
    renderer.registerTtfFont(3, &ttf);
    baseline.fontMap = renderer.getFontMap();
    baseline.sdCardFonts_ = renderer.getSdCardFonts();
    raw = renderer.textProvider();
  }
  ~Fixture() { activeFixture = nullptr; }
  XpNativeProvider provider() {
    auto p = raw;
    p.context = this;
    p.family = [](void* c, int32_t id, XpNativeFamily* out) {
      auto& f = *static_cast<Fixture*>(c);
      ++f.setups;
      f.families |= 1u << id;
      if (id == 2 && f.evictions) ++f.sdAfterEviction;
      f.raw.family(f.raw.context, id, out);
    };
    p.glyph = [](void* c, int32_t id, uint32_t cp, uint8_t s, XpTextmGlyph* out) {
      auto& f = *static_cast<Fixture*>(c);
      return f.raw.glyph(f.raw.context, id, cp, s, out);
    };
    p.coverage = [](void* c, int32_t id, uint32_t cp, uint8_t s) {
      auto& f = *static_cast<Fixture*>(c);
      return f.raw.coverage(f.raw.context, id, cp, s);
    };
    p.kern = [](void* c, int32_t id, uint32_t a, uint32_t b, uint8_t s) {
      auto& f = *static_cast<Fixture*>(c);
      return f.raw.kern(f.raw.context, id, a, b, s);
    };
    p.prewarm = [](void* c, int32_t id, const uint8_t* text, size_t len, uint8_t s) {
      auto& f = *static_cast<Fixture*>(c);
      f.raw.prewarm(f.raw.context, id, text, len, s);
    };
    return p;
  }
};
int32_t oldAdvance(Fixture& f, int id, const char* word, uint8_t style, int8_t tracking = 0) {
  return f.baseline.getTextAdvanceX(id, word, static_cast<EpdFontFamily::Style>(style), tracking,
                                    BidiUtils::BidiBaseDir::AUTO, FrozenRenderer::TextMeasureMode::Layout);
}
uint8_t startsRtl(XpParaSpan s, int32_t depth) {
  return BidiUtils::startsWithRtl({reinterpret_cast<const char*>(s.data), s.len}, depth);
}
XpParaMetrics metrics(Fixture& f, bool native) {
  XpParaMetrics m{};
  m.context = &f;
  m.prepare = [](void* c, int32_t id, const XpParaSpan* spans, uint32_t count, uint8_t space, uint8_t hyphen,
                 uint8_t styles, uint8_t small) {
    std::vector<const char*> segments;
    std::vector<size_t> lengths;
    for (uint32_t i = 0; i < count; ++i) {
      segments.push_back(reinterpret_cast<const char*>(spans[i].data));
      lengths.push_back(spans[i].len);
    }
    static_cast<Fixture*>(c)->renderer.ensureSdCardFontReady(id, segments.data(), lengths.data(), count, space, hyphen,
                                                             styles, small);
  };
  if (native) {
    m.native = [](void* c, XpNativeProvider* out) -> uint8_t {
      *out = static_cast<Fixture*>(c)->provider();
      return 1;
    };
    return m;
  }
  m.advance = [](void* c, int32_t id, XpParaSpan s, uint8_t style, int8_t tracking) {
    return oldAdvance(*static_cast<Fixture*>(c), id, s.len ? reinterpret_cast<const char*>(s.data) : "", style,
                      tracking);
  };
  m.space = [](void* c, int32_t id, uint8_t s) -> int32_t {
    return static_cast<Fixture*>(c)->baseline.getSpaceWidth(id, static_cast<EpdFontFamily::Style>(s));
  };
  m.kerning = [](void* c, int32_t id, uint32_t a, uint32_t b, uint8_t s, int8_t t) -> int32_t {
    return static_cast<Fixture*>(c)->baseline.getKerning(id, a, b, static_cast<EpdFontFamily::Style>(s), t);
  };
  m.space_advance = [](void* c, int32_t id, uint32_t a, uint32_t b, uint8_t s) -> int32_t {
    return static_cast<Fixture*>(c)->baseline.getSpaceAdvance(id, a, b, static_cast<EpdFontFamily::Style>(s));
  };
  m.ascender = [](void* c, int32_t id) -> int32_t {
    return static_cast<Fixture*>(c)->baseline.getFontAscenderSize(id);
  };
  m.is_sd = [](void*, int32_t id) -> uint8_t { return id == 2; };
  m.starts_rtl = startsRtl;
  m.visual_order = [](XpParaSpan s, const uint16_t* offsets, uint32_t count, uint8_t rtl, uint16_t* out) -> uint8_t {
    return BidiUtils::computeVisualWordOrder({reinterpret_cast<const char*>(s.data), s.len}, {offsets, count}, rtl,
                                             {out, count});
  };
  return m;
}
struct Result {
  std::vector<std::string> lines;
  unsigned words = 0, focused = 0;
};
std::vector<int32_t> measurements(const char* sd, const char* ttf, bool native) {
  Fixture f(sd, ttf, true);
  f.renderer.ensureSdCardFontReady(2, "AVATAR office");
  struct Context {
    Fixture& f;
    std::vector<int32_t> values;
  } context{f, {}};
  auto sequence = [](void* c, const void* engine) {
    auto& ctx = *static_cast<Context*>(c);
    // Three stable families force replacement in the two-family cache, without invalidation.
    const auto generation = fontTableGeneration.load();
    for (int id : {1, 2, 4, 1, 2, 4}) {
      const char* word = "AVATAR";
      ctx.values.push_back(
          engine ? xp_native_test_advance(engine, id, reinterpret_cast<const uint8_t*>(word), strlen(word), 0)
                 : oldAdvance(ctx.f, id, word, 0));
    }
    require(fontTableGeneration.load() == generation, "replacement phase unexpectedly mutated fonts");
    if (engine) require(ctx.f.setups == 6, "three-family descriptor replacement not exercised");
    for (int id : {2, 3, 2, 4, 1, 3, 2})
      for (uint8_t style : {0, 1, 2, 3, 4, 8, 16}) {
        const char* word = "office\xF4\x8F\xBF\xBF";
        ctx.values.push_back(
            engine ? xp_native_test_advance(engine, id, reinterpret_cast<const uint8_t*>(word), strlen(word), style)
                   : oldAdvance(ctx.f, id, word, style));
      }
    require(ctx.f.evictions == 1, "measurement callback eviction missing");
    if (engine) require(ctx.f.sdAfterEviction > 0, "engine did not refresh SD after callback eviction");
  };
  auto provider = f.provider();
  if (native)
    xp_native_test_run(&provider, &context, sequence);
  else
    sequence(&context, nullptr);
  return context.values;
}
Result paragraph(const char* sd, const char* ttf, bool native, bool evict, uint8_t focus, uint8_t hyphen,
                 uint16_t width) {
  Fixture f(sd, ttf, evict);
  auto m = metrics(f, native);
  alignas(uintptr_t) uintptr_t storage[64]{};
  auto* state = reinterpret_cast<XpParagraph*>(storage);
  const XpParaAllocator allocator{std::malloc, std::free};
  XpParaBlock block{};
  block.font_scale = 1;
  xp_para_init(state, &allocator, &block, hyphen, focus, 0, startsRtl);
  std::array<uint8_t, 4> slots{};
  for (int i = 1; i < 4; ++i) {
    slots[i] = xp_para_size_slot(state, 1.0f + i * 0.1f);
    require(slots[i] != 0, "font slot allocation");
    xp_para_slot_font(state, slots[i], i + 1);
  }
  constexpr const char* words[] = {"AVATAR",      "office",    "discretionary",     "internationalization",
                                   "measurement", "rendering", "a\xF4\x8F\xBF\xBF", "hyphenation"};
  uint32_t offset = 0;
  for (int i = 0; i < 40; ++i) {
    const char* word = words[i % std::size(words)];
    // SD before the first regular TTF fault, and again afterward, within one engine.
    const uint8_t slot = slots[(i + 1) % 4];
    const uint8_t style = i == 1 ? 0 : (i / 4) % 4;
    xp_para_add_word(state, {reinterpret_cast<const uint8_t*>(word), static_cast<uint32_t>(strlen(word))}, style, 0, 0,
                     offset, 0, slot, 0);
    offset += strlen(word) + 1;
  }
  Result result;
  XpParaLayout options{
      1, width, 1, 0, 100, UINT32_MAX, 0, nullptr, &result, [](void* c, const XpParaLine* line) -> uint8_t {
        auto& r = *static_cast<Result*>(c);
        std::ostringstream out;
        out << line->paragraph_start << ':' << line->visible_offset << ':' << line->count << ':';
        out.write(reinterpret_cast<const char*>(line->text.data), line->text.len);
        for (uint32_t i = 0; i < line->count; ++i) {
          out << '/' << line->offsets[i] << ',' << line->positions[i] << ',' << int(line->styles[i]) << ','
              << (line->boundaries ? int(line->boundaries[i]) : -1) << ','
              << (line->suffixes ? int(line->suffixes[i]) : -1) << ',' << line->sources[2 * i] << ','
              << line->sources[2 * i + 1] << ',' << (line->font_slots ? int(line->font_slots[i]) : 0);
          if (line->boundaries && line->boundaries[i]) ++r.focused;
          require(line->ruby[i].len == 0, "unexpected ruby");
        }
        if (line->fonts)
          for (int i = 0; i < 3; ++i) out << ':' << line->fonts[i];
        require(line->link_count == 0, "unexpected links");
        r.words += line->count;
        r.lines.push_back(out.str());
        return 1;
      }};
  xp_para_layout(state, &m, &options);
  require(!xp_para_dropped(state), "paragraph OOM");
  xp_para_destroy(state);
  require(!result.lines.empty(), "no lines emitted");
  require(f.evictions == unsigned(evict), "paragraph glyph-callback eviction not exercised");
  if (native) {
    require(f.families == 0x1e, "paragraph did not use all four families");
    if (evict) require(f.sdAfterEviction > 0, "paragraph engine did not refresh SD after eviction");
  }
  return result;
}
}  // namespace
int main(int argc, char** argv) {
  require(argc == 3, "NativeParagraph public.cpfont public.ttf");
  const auto expected = measurements(argv[1], argv[2], false);
  require(measurements(argv[1], argv[2], true) == expected, "persistent-engine measurements differ from C++");
  unsigned cases = 0, lines = 0, splits = 0, focused = 0;
  for (bool evict : {false, true})
    for (uint8_t focus : {0, 1})
      for (uint8_t hyphen : {0, 1})
        for (uint16_t width : {80, 240}) {
          const auto old = paragraph(argv[1], argv[2], false, evict, focus, hyphen, width);
          const auto current = paragraph(argv[1], argv[2], true, evict, focus, hyphen, width);
          if (old.lines != current.lines) {
            std::fprintf(stderr, "paragraph mismatch: eviction=%d focus=%u hyphen=%u width=%u\n", evict, focus, hyphen,
                         width);
            return 1;
          }
          if (hyphen && current.words > 40) ++splits;
          focused += current.focused;
          lines += current.lines.size();
          ++cases;
        }
  require(splits > 0 && focused > 0, "hyphenation/focus output not exercised");
  std::printf(
      "native paragraph: %u cases, %u lines, %zu measurements identical; %u split cases, %u focus boundaries; "
      "four families, descriptor replacement and glyph-callback SD eviction passed\n",
      cases, lines, expected.size(), splits, focused);
}
