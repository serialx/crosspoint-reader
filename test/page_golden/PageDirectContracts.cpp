#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <Page.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <xp_page_paint.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

static std::vector<std::string> directBatches;
void contractResident(const uint8_t* bytes, size_t length) {
  directBatches.emplace_back(reinterpret_cast<const char*>(bytes), length);
}

namespace {
void require(bool value, const char* reason) {
  if (!value) {
    std::cerr << reason << '\n';
    std::exit(1);
  }
}
std::vector<uint8_t> arena(int font, const std::string& word = "Variant") {
  Page page;
  BlockStyle style;
  style.fontId = font;
  auto text = std::make_unique<TextBlock>(std::vector<std::string>{word}, std::vector<int16_t>{0},
                                          std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR},
                                          std::vector<uint8_t>{}, std::vector<uint16_t>{}, style);
  page.elements.push_back(std::make_unique<PageLine>(std::move(text), 0, 0));
  {
    HalFile file("direct-contract.bin");
    require(page.serialize(file), "serialize direct contract");
  }
  std::ifstream file("direct-contract.bin", std::ios::binary);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  XpPageInfo info{};
  require(xp_page_arena_size(bytes.data(), bytes.size(), &info) == 0, "size direct contract");
  std::vector<uint8_t> result(info.arena_size);
  require(xp_page_decode(bytes.data(), bytes.size(), 0, result.data(), result.size(), &info) == 0,
          "decode direct contract");
  return result;
}
struct Loader {
  GfxRenderer* renderer;
  EpdFont* variant;
  unsigned calls = 0;
  int mode = 0;
};
bool load(void* raw, int id) {
  auto& state = *static_cast<Loader*>(raw);
  require(id == 101, "production loader forwarded the wrong font ID");
  ++state.calls;
  if (state.mode == 1) return false;
  if (state.mode == 2) return true;
  state.renderer->removeFont(102);
  state.renderer->insertFont(id, EpdFontFamily(state.variant));
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  require(argc == 2, "usage: PageDirectContracts public-Hebrew.cpfont");
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  EpdFont section(&notosans_12_regular), variant(&notosans_14_regular);
  renderer.insertFont(1, EpdFontFamily(&section));
  FontDecompressor decompressor;
  require(decompressor.init(), "initialize decompressor");
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  Loader state{&renderer, &variant};
  renderer.setFontVariantProvider({&state, nullptr, load});
  const auto requested = arena(101), fallback = arena(1);
  auto paint = [&](const auto& bytes) {
    renderer.clearScreen();
    XpPageTarget target{};
    renderer.pageTarget(target);
    const XpPagePaint request{1, 10, 10, 0, nullptr, nullptr, 0, 0};
    require(xp_page_paint(bytes.data(), bytes.size(), &target, &request) == 0, "paint direct contract");
    return display.pixels;
  };
  const auto fallbackPixels = paint(fallback);
  renderer.insertFont(101, EpdFontFamily(&variant));
  const auto variantPixels = paint(requested);
  require(variantPixels != fallbackPixels, "fixture must distinguish selected-font pixels");
  renderer.removeFont(101);
  renderer.insertFont(102, EpdFontFamily(&variant));
  require(paint(requested) == variantPixels, "absent variant did not paint with the registered font");
  require(state.calls == 1 && renderer.getFontMap().count(101) && !renderer.getFontMap().count(102),
          "successful production loader did not register/evict exactly once");
  paint(requested);
  require(state.calls == 1, "already-loaded font reentered native loader");
  for (int mode : {1, 2}) {
    renderer.removeFont(101);
    state.mode = mode;
    const auto before = state.calls;
    require(paint(requested) == fallbackPixels, "failed/unregistered font did not use section-font pixels");
    require(state.calls > before && !renderer.getFontMap().count(101), "loader postcondition was not checked");
  }
  std::cout << "PASS production direct loader: absent registration, loaded bypass, failure, false success, eviction "
               "and pixels\n";
  SdCardFont hebrew;
  require(hebrew.load(argv[1]), "load public Hebrew fallback");
  renderer.insertFont(202, EpdFontFamily(hebrew.getEpdFont(0)));
  renderer.registerSdCardFont(202, &hebrew);
  renderer.setFallbackFont(1, 202);
  require(!section.hasCodepoint(0x5d0) && hebrew.getEpdFont(0)->hasCodepoint(0x5d0),
          "partition fixture must redirect to SD");
  std::string word, visual;
  for (int i = 0; i < 50; ++i) {
    word += "\xd7\x90\xd7\x91";
    visual += "\xd7\x91\xd7\x90";
  }
  const auto redirected = arena(1, word);
  const std::vector<std::string> expected{visual.substr(0, 190), visual.substr(190)};
  paint(redirected);
  require(directBatches == expected, "direct residency batches differ from the frozen legacy 190/10-byte result");
  std::cout << "PASS redirected UTF-8 residency: exact legacy 190/10-byte batches and visual order\n";
}
