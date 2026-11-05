#include <fstream>
#include <iterator>
#undef malloc
#undef free
extern "C" size_t xp_test_live_allocations();
extern "C" uint8_t xp_epub_test_section_driver(const XpAppResources*, const XpEpubLeaves*, const XpAppRuntime*,
                                               XpAppText, XpAppText, uint8_t, uint8_t,
                                               void (*)(void*, const XpEpubPageView*, uint16_t, uint8_t, uint32_t));
static uint32_t expectedVisible;
static void native(void* raw, const XpEpubPageView* rawPage, uint16_t pages, uint8_t partial, uint32_t popups) {
  auto& handle = book(raw);
  assert(handle.value().getCssParser() == nullptr);
  assert(handle.book.use_count() == 1);
  const Page page(Page::Borrowed{}, rawPage->arena, rawPage->size, rawPage->visible_offset);
  assert(page.elementCount() > 0);
  Section section(handle.book, 0, driverRenderer);
  assert(section.getCachedPageCount() == (partial ? std::nullopt : std::optional<uint16_t>(pages)));
  assert(section.getPageForAnchor("anchor") == 0);
  assert(section.getPageForParagraphIndex(0) == 0);
  assert(section.getPageForListItemIndex(0) == 0);
  assert(section.getPageForVisibleTextOffset(0) == 0);
  CrossPointPosition position{0, 0, pages};
  position.visibleTextOffset = 10;
  position.hasVisibleTextOffset = true;
  const auto saved = ProgressMapper::toSavedProgress(handle.book, position);
  assert(!saved.xpath.empty());
  const auto restored = ProgressMapper::toCrossPoint(handle.book, saved, driverRenderer, 0, pages);
  assert(restored.spineIndex == 0 && restored.hasVisibleTextOffset && restored.visibleTextOffset == expectedVisible);
  std::printf(
      "Section driver: %u pages, partial=%u, popups=%u; actual ReaderSection, native Page and ProgressMapper.\n", pages,
      partial, popups);
}
int main(int argc, char** argv) {
  assert(argc == 6);
  expectedVisible = std::stoul(argv[5]);
  std::string path = argv[1], cache = argv[2];
  const bool partial = std::string(argv[3]) == "partial";
  const bool popup = std::string(argv[4]) == "popup";
  sectionMetrics.context = &driverRenderer;
  {
    CssParser css(cache);
    HalFile f;
    assert(f.open((cache + "/source.css").c_str(), "rb"));
    assert(css.loadFromStream(f) == CssParser::ParseResult::Complete);
    assert(css.saveToCache(true));
  }
  static const XpAppRuntime runtime = [] {
    auto value = unexpectedAppRuntime();
    value.now_ms = []() -> uint32_t { return 0; };
    value.delay_ms = [](uint32_t) {};
    return value;
  }();
  assert(xp_epub_test_section_driver(&resources, &leaves, &runtime, text(path), text(cache), partial, popup, native));
  assert(driverRenderer.loanDepth == 0);
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
}
