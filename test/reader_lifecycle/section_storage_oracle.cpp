#include <array>
#include <fstream>
#include <iterator>
#undef malloc
#undef free
extern "C" uint8_t xp_epub_test_section_storage(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                                void (*)(void*, uint8_t, uint32_t (*)[20], uint32_t (*)[4]));
extern "C" size_t xp_test_live_allocations();
// ACTUAL_HEADER_FIELDS
static std::vector<char> original;
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
  spec.imageRendering = 0;
  spec.lineCompression = 1.0f;
  spec.wordSpacingPercent = 100;
  const auto path = handle.value().getCachePath() + "/sections/0.bin";
  Section section(handle.book, 0, renderer);
  if (phase == 0) {
    assert(section.createSectionFile(spec));
    assert(section.isBuildComplete() && section.pageCount > 2);
    original = bytes(path);
    auto fields = headerFields(spec, section.pageCount);
    std::copy(fields.begin(), fields.end(), *expected);
    (*values)[0] = section.pageCount;
    (*values)[1] = section.getParagraphIndexForPage(section.pageCount - 1).value();
    (*values)[2] = section.getVisibleTextOffsetForPage(section.pageCount - 1).value();
    (*values)[3] = section.getPageForAnchor("anchor").value();
  } else {
    assert(bytes(path) == original);
    assert(section.loadSectionFile(spec));
    assert(section.loadPage(0));
    CrossPointPosition position{0, 0, section.pageCount};
    position.visibleTextOffset = 10;
    position.hasVisibleTextOffset = true;
    const auto saved = ProgressMapper::toSavedProgress(handle.book, position);
    assert(!saved.xpath.empty());
    const auto restored = ProgressMapper::toCrossPoint(handle.book, saved, renderer, 0, section.pageCount);
    assert(restored.spineIndex == 0 && restored.hasVisibleTextOffset && restored.visibleTextOffset == 10);
    std::printf(
        "Section storage roundtrip: %zu identical bytes, %u pages; native Page and ProgressMapper consumed Rust "
        "commit.\n",
        original.size(), section.pageCount);
  }
}
int main(int argc, char** argv) {
  assert(argc == 3);
  std::string path = argv[1], cache = argv[2];
  {
    CssParser css(cache);
    HalFile f;
    assert(f.open((cache + "/source.css").c_str(), "rb"));
    assert(css.loadFromStream(f) == CssParser::ParseResult::Complete);
    assert(css.saveToCache(true));
  }
  assert(xp_epub_test_section_storage(&resources, &leaves, text(path), text(cache), native));
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
}
