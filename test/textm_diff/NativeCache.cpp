#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <xp_ffi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void xp_native_test_run(const XpNativeProvider*, void*, void (*)(void*, const void*));
extern "C" int32_t xp_native_test_advance(const void*, int32_t, const uint8_t*, size_t, uint8_t);
namespace {
void require(bool ok, const char* why) {
  if (!ok) {
    std::fprintf(stderr, "%s\n", why);
    std::exit(1);
  }
}
struct Test {
  GfxRenderer& renderer;
  SdCardFont& sd;
  const char* path;
  XpNativeProvider raw;
  unsigned setups = 0, misses = 0;
};
void bind(Test& t) {
  t.renderer.removeFont(2);
  t.renderer.insertFont(2,
                        EpdFontFamily(t.sd.getEpdFont(0), t.sd.getEpdFont(1), t.sd.getEpdFont(2), t.sd.getEpdFont(3)));
  t.renderer.registerSdCardFont(2, &t.sd);
}
void sequence(void* ctx, const void* cache) {
  auto& t = *static_cast<Test*>(ctx);
  const char* word = "AVATAR";
  auto cached = [&](int id, uint8_t style = 0) {
    return xp_native_test_advance(cache, id, reinterpret_cast<const uint8_t*>(word), strlen(word), style);
  };
  auto check = [&](const char* event) {
    for (int id : {1, 2})
      for (uint8_t style = 0; style < 4; ++style) {
        const auto expected = t.renderer.getTextAdvanceX(id, word, static_cast<EpdFontFamily::Style>(style));
        require(cached(id, style) == expected, event);
      }
  };
  t.renderer.ensureSdCardFontReady(2, word);
  check("prepared");
  const auto before = t.setups;
  for (int n = 0; n < 100; ++n)
    for (int id : {1, 2})
      for (uint8_t style = 0; style < 4; ++style) cached(id, style);
  require(t.setups == before, "steady words called descriptor setup");
  require(t.misses == 0, "prepared words called glyph miss");
  t.sd.buildAdvanceTable("office ffi xyz");
  check("advance rebuild");
  t.sd.prewarm("AVATAR office ffi", 0x0f, false, true);
  check("prewarm");
  t.sd.getEpdFont(0)->getGlyph(0x2014);
  check("overflow miss");
  t.sd.clearCache();
  check("cache eviction");
  t.sd.clearPersistentCache();
  check("advance release");
  t.sd.releaseResidentCaches();
  check("all caches released");
  t.renderer.removeFont(2);
  require(cached(2) == 0, "unregister");
  require(t.sd.load(t.path), "reload");
  bind(t);
  check("font reload");
  t.renderer.setFallbackFont(1, 2);
  word = "مرحبا";
  check("fallback registered");
  t.renderer.clearFallbackFonts();
  check("fallback cleared");
  std::printf("native cache: mutation checks passed; steady-state setup/miss callbacks 0/0; total setup=%u miss=%u\n",
              t.setups, t.misses);
}
}  // namespace
#include "NativeLayout.h"
int main(int argc, char** argv) {
  require(argc == 2 || argc == 3, "NativeCache public.cpfont [--layout-count]");
  HalDisplay display;
  GfxRenderer renderer(display);
  static EpdFont regular(&notosans_12_regular), bold(&notosans_12_bold);
  renderer.insertFont(1, EpdFontFamily(&regular, &bold));
  SdCardFont sd;
  require(sd.load(argv[1]), "load");
  Test test{renderer, sd, argv[1], renderer.textProvider()};
  bind(test);
  auto provider = test.raw;
  provider.context = &test;
  provider.family = [](void* c, int32_t id, XpNativeFamily* out) {
    auto& t = *static_cast<Test*>(c);
    ++t.setups;
    t.raw.family(t.raw.context, id, out);
  };
  provider.glyph = [](void* c, int32_t id, uint32_t cp, uint8_t style, XpTextmGlyph* out) -> uint8_t {
    auto& t = *static_cast<Test*>(c);
    ++t.misses;
    return t.raw.glyph(t.raw.context, id, cp, style, out);
  };
  provider.coverage = [](void* c, int32_t id, uint32_t cp, uint8_t style) -> uint8_t {
    auto& t = *static_cast<Test*>(c);
    return t.raw.coverage(t.raw.context, id, cp, style);
  };
  provider.kern = [](void* c, int32_t id, uint32_t a, uint32_t b, uint8_t s) -> int8_t {
    auto& t = *static_cast<Test*>(c);
    return t.raw.kern(t.raw.context, id, a, b, s);
  };
  provider.prewarm = [](void* c, int32_t id, const uint8_t* p, size_t n, uint8_t s) {
    auto& t = *static_cast<Test*>(c);
    t.raw.prewarm(t.raw.context, id, p, n, s);
  };
  if (argc == 3 && strcmp(argv[2], "--layout-count") == 0) {
    nativeLayout(test, provider);
    return 0;
  }
  xp_native_test_run(&provider, &test, sequence);
}
