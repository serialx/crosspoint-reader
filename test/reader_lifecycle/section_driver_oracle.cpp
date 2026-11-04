#include <BidiUtils.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/hyphenation/Hyphenator.h>

#include <array>
#include <fstream>
#include <iterator>
#undef malloc
#undef free
extern "C" size_t xp_test_live_allocations();
const XpParaAllocator* paragraphAllocator();
static GfxRenderer driverRenderer;
static const GfxRenderer& physicalRenderer(void* raw) {
  assert(raw == &driverRenderer);
  return *static_cast<GfxRenderer*>(raw);
}
// NATIVE_METRICS
struct DriverBindings {
  const XpParaMetrics* metrics;
  const XpParaAllocator* allocator;
  int32_t (*variant)(int32_t, float);
  int32_t (*height)(int32_t, float);
  int32_t (*ready)(int32_t, int32_t);
  uint8_t (*codepoint)(int32_t, uint32_t, uint8_t, int32_t*, int32_t*);
  uint8_t (*supported)(XpAppText);
  uint8_t (*dimensions)(XpAppText, uint16_t*, uint16_t*);
  void (*flush)(void*);
  uint8_t (*lend)();
  void (*restore)(uint8_t);
  void (*delay)(uint32_t);
  uint32_t (*now)();
  int32_t (*language)(XpAppText);
};
static const DriverBindings bindings{
    &driverMetrics,
    paragraphAllocator(),
    [](int32_t font, float scale) { return driverRenderer.resolveFontVariant(font, scale); },
    [](int32_t font, float compression) { return driverRenderer.getLineHeight(font, compression); },
    [](int32_t font, int32_t fallback) {
      return font == fallback || driverRenderer.ensureFontLoaded(font) ? font : fallback;
    },
    [](int32_t font, uint32_t cp, uint8_t style, int32_t* advance, int32_t* top) -> uint8_t {
      int measuredTop = 0;
      bool result =
          driverRenderer.getCodepointMetrics(font, cp, static_cast<EpdFontFamily::Style>(style), *advance, measuredTop);
      *top = measuredTop;
      return result;
    },
    [](XpAppText path) -> uint8_t { return ImageDecoderFactory::isFormatSupported(pathText(path)); },
    [](XpAppText, uint16_t*, uint16_t*) -> uint8_t { std::abort(); },
    [](void* raw) { static_cast<HalFile*>(raw)->flush(); },
    []() -> uint8_t {
      ++driverRenderer.loanDepth;
      return 1;
    },
    [](uint8_t token) {
      assert(token == 1 && driverRenderer.loanDepth > 0);
      --driverRenderer.loanDepth;
    },
    [](uint32_t) {},
    []() -> uint32_t { return 0; },
    [](XpAppText language) {
      Hyphenator::setPreferredLanguage(owned(language));
      return Hyphenator::preferredLanguageId();
    }};
extern "C" uint8_t xp_epub_test_section_driver(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                               const DriverBindings*, uint8_t,
                                               void (*)(void*, uint8_t, uint32_t (*)[20], uint32_t (*)[4]));
// ACTUAL_HEADER_FIELDS
static bool partial;
static std::vector<char> original;
static CrossPointPosition parentMapping;
static CrossPointPosition mapPosition(BookHandle& handle, GfxRenderer& renderer, int pages) {
  CrossPointPosition position{0, 0, pages};
  position.visibleTextOffset = 10;
  position.hasVisibleTextOffset = true;
  const auto saved = ProgressMapper::toSavedProgress(handle.book, position);
  assert(!saved.xpath.empty());
  return ProgressMapper::toCrossPoint(handle.book, saved, renderer, 0, pages);
}
static std::vector<char> bytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void native(void* raw, uint8_t phase, uint32_t (*expected)[20], uint32_t (*values)[4]) {
  auto& handle = book(raw);
  GfxRenderer renderer;
  ReaderRenderSpec spec;
  spec.fontId = 16;
  spec.viewportWidth = 240;
  spec.viewportHeight = 320;
  spec.embeddedStyle = true;
  spec.imageRendering = 1;
  spec.lineCompression = 1.0f;
  spec.wordSpacingPercent = 100;
  const auto path = handle.value().getCachePath() + "/sections/0.bin";
  if (phase == 0) {
    {
      Section section(handle.book, 0, renderer);
      if (partial) {
        assert(section.startBuild(spec));
        assert(section.buildSomeMore(1));
        assert(section.isBuilding() && section.pageCount > 0);
      } else {
        assert(section.createSectionFile(spec));
        assert(section.isBuildComplete() && section.pageCount > 2);
      }
      auto fields = headerFields(spec, section.pageCount);
      std::copy(fields.begin(), fields.end(), *expected);
      (*values)[0] = section.pageCount;
    }
    original = bytes(path);
    parentMapping = mapPosition(handle, renderer, (*values)[0]);
  } else {
    const auto candidate = bytes(path);
    if (candidate != original) {
      auto mismatch = std::mismatch(candidate.begin(), candidate.end(), original.begin(), original.end());
      std::fprintf(stderr, "driver mismatch: candidate=%zu parent=%zu offset=%zu\n", candidate.size(), original.size(),
                   size_t(mismatch.first - candidate.begin()));
      std::abort();
    }
    Section section(handle.book, 0, renderer);
    assert(section.loadSectionFile(spec));
    assert(section.isPartial() == partial && section.pageCount == (*values)[0]);
    assert(section.loadPage(0));
    const auto restored = mapPosition(handle, renderer, section.pageCount);
    assert(restored.spineIndex == parentMapping.spineIndex);
    assert(restored.hasVisibleTextOffset == parentMapping.hasVisibleTextOffset);
    assert(restored.visibleTextOffset == parentMapping.visibleTextOffset);
    std::printf(
        "Section driver: %zu identical bytes, %u pages, partial=%u; native Page and ProgressMapper consumed Rust "
        "build.\n",
        original.size(), section.pageCount, unsigned(partial));
  }
}
int main(int argc, char** argv) {
  assert(argc == 4);
  std::string path = argv[1], cache = argv[2];
  partial = std::string(argv[3]) == "partial";
  {
    CssParser css(cache);
    HalFile f;
    assert(f.open((cache + "/source.css").c_str(), "rb"));
    assert(css.loadFromStream(f) == CssParser::ParseResult::Complete);
    assert(css.saveToCache(true));
  }
  assert(xp_epub_test_section_driver(&resources, &leaves, text(path), text(cache), &bindings, partial, native));
  assert(driverRenderer.loanDepth == 0);
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
}
