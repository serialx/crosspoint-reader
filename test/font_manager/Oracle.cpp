#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <cstdio>

static SdCardFontFamilyInfo family(const char* name) {
  SdCardFontFamilyInfo f;
  f.name = name;
  f.root = "/fonts";
  for (uint8_t size : {14, 18, 20}) f.files.push_back({nullptr, size, 0});
  return f;
}
static void state(const SdCardFontManager& manager, const GfxRenderer& renderer) {
  std::printf("state %s %u %zu %u\n", manager.currentFamilyName().c_str(), manager.currentPointSize(),
              renderer.getFontMap().size(), SdCardFont::live);
}
int main() {
  for (int mode = 0; mode < 4; ++mode) {
    std::printf("CASE %d\n", mode);
    GfxRenderer renderer;
    auto f = family("Example");
    {
      SdCardFontManager manager;
      SdCardFont::failCreate = mode == 1;
      SdCardFont::failLoad = mode == 2;
      if (mode == 3) f.name.assign(150, 'x');
      std::printf("family %d\n", manager.loadFamily(f, renderer, 16));
      state(manager, renderer);
      SdCardFont::failCreate = SdCardFont::failLoad = false;
      if (mode) continue;
      std::printf("lookup %d %d %d\n", manager.getFontId("Example"), manager.getFontId("Other"),
                  manager.getFontId("Example", 18));
      std::printf("reuse %d\n", manager.loadFamilyExtraSize(f, renderer, 14, 100));
      std::printf("extra %d\n", manager.loadFamilyExtraSize(f, renderer, 18, 101));
      std::printf("variant %d\n", manager.loadFamilyVariant(f, renderer, 18, 102));
      std::printf("collision %d\n", manager.loadFamilyVariant(f, renderer, 20, 102));
      std::printf("missing %d\n", manager.loadFamilyExtraSize(f, renderer, 19, 103));
      manager.unloadFont(renderer, 101);
      manager.unloadFont(renderer, 999);
      state(manager, renderer);
      auto second = family("Second");
      std::printf("replace %d\n", manager.loadFamily(second, renderer, 20));
      state(manager, renderer);
      second.files.clear();
      std::printf("empty %d\n", manager.loadFamily(second, renderer, 14));
      state(manager, renderer);
      std::printf("reload %d\n", manager.loadFamily(f, renderer, 18));
    }
    std::printf("final live %u\n", SdCardFont::live);
  }
  {
    GfxRenderer renderer;
    SdCardFontManager manager;
    auto f = family("Embedded");
    f.name.append("\0suffix", 7);
    std::printf("embedded %d\n", manager.loadFamily(f, renderer, 14));
    std::printf("names %zu %d %d\n", manager.currentFamilyName().size(), manager.getFontId(f.name),
                manager.getFontId("Embedded"));
  }
}
