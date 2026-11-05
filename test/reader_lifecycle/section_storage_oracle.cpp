#include <array>
#include <fstream>
#include <iterator>

#include "../section_io/SectionTestCodec.h"
#undef malloc
#undef free
extern "C" uint8_t xp_epub_test_section_storage(const XpAppResources*, const XpEpubLeaves*, const XpAppRuntime*,
                                                const XpEpubRenderSpec*, XpAppText, XpAppText,
                                                void (*)(void*, uint8_t, uint32_t (*)[20], uint32_t (*)[4]));
extern "C" size_t xp_test_live_allocations();
static std::vector<char> original;
static std::vector<char> bytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
static void native(void* raw, uint8_t phase, uint32_t (*expected)[20], uint32_t (*values)[4]) {
  auto& handle = book(raw);
  const auto path = handle.value().getCachePath() + "/sections/0.bin";
  Section section(handle.book, 0, driverRenderer);
  const auto pages = section.getCachedPageCount().value();
  assert(pages > 2 && handle.value().getCssParser() == nullptr);
  const auto decode = [](XpSecKind kind, size_t offset) {
    assert(offset < original.size());
    XpSecDecoder decoder{reinterpret_cast<const uint8_t*>(original.data()), original.size(), offset, kind};
    XpSecRecord record{};
    assert(xp_sec_decode_next(&decoder, &record) == XP_SEC_OK);
    return record;
  };
  if (phase == 0) {
    original = bytes(path);
    const auto header = decode(XP_SEC_HEADER, 0);
    std::copy(std::begin(header.values), std::end(header.values), *expected);
    assert(header.values[14] == pages);
    (*values)[0] = pages;
    (*values)[1] = decode(XP_SEC_U16, header.values[17] + 2 + (pages - 1) * 2).values[0];
    (*values)[2] = decode(XP_SEC_U32, header.values[19] + (pages - 1) * 4).values[0];
    (*values)[3] = section.getPageForAnchor("anchor").value();
  } else {
    assert(bytes(path) == original);
    const auto header = decode(XP_SEC_HEADER, 0);
    const auto first = decode(XP_SEC_U32, header.values[15]).values[0];
    const auto next = decode(XP_SEC_U32, header.values[15] + 4).values[0];
    assert(next > first && next <= original.size());
    auto page = Page::fromBytes(reinterpret_cast<const uint8_t*>(original.data()) + first, next - first);
    assert(page && page->elementCount() > 0);
    CrossPointPosition position{0, 0, pages};
    position.visibleTextOffset = 10;
    position.hasVisibleTextOffset = true;
    const auto saved = ProgressMapper::toSavedProgress(handle.book, position);
    assert(!saved.xpath.empty());
    const auto restored = ProgressMapper::toCrossPoint(handle.book, saved, driverRenderer, 0, pages);
    assert(restored.spineIndex == 0 && restored.hasVisibleTextOffset && restored.visibleTextOffset == 10);
    std::printf(
        "Section storage roundtrip: %zu identical bytes, %u pages; native Page and ProgressMapper consumed Rust "
        "commit.\n",
        original.size(), pages);
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
  sectionMetrics.context = &driverRenderer;
  static const XpAppRuntime runtime = [] {
    auto value = unexpectedAppRuntime();
    value.now_ms = []() -> uint32_t { return 0; };
    value.delay_ms = [](uint32_t) {};
    return value;
  }();
  const XpEpubRenderSpec spec{16, 1.0f, 240, 320, 0, 0, 0, 100, 0, 0, 1, 0, 0};
  assert(xp_epub_test_section_storage(&resources, &leaves, &runtime, &spec, text(path), text(cache), native));
  assert(driverRenderer.loanDepth == 0);
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
}
