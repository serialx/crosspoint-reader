#include <mutex>
#include <thread>
#undef malloc
#undef free
extern "C" {
void* xp_epub_test_section_fixture_open(const XpAppResources*, const XpEpubLeaves*, const XpAppRuntime*, XpAppText,
                                        XpAppText, int32_t);
void* xp_epub_test_section_fixture_facade(void*);
void xp_epub_test_section_fixture_close(void*);
uint8_t xp_epub_test_section_fixture_start(void*, const XpEpubRenderSpec*, XpEpubBuildPopup, void*);
uint8_t xp_epub_test_section_fixture_advance(void*, int32_t, uint8_t);
uint8_t xp_epub_test_section_fixture_load(void*, const XpEpubRenderSpec*);
void xp_epub_test_section_fixture_facts(void*, uint32_t (*)[4]);
void* xp_epub_test_section_fixture_page(void*, int32_t);
size_t xp_test_live_allocations();
}
static std::mutex renderMutex;
static bool buildOnRenderThread(void* section) {
  bool result = false;
  std::thread render([&] {
    std::lock_guard<std::mutex> lock(renderMutex);
    result = xp_epub_test_section_fixture_advance(section, 1, 0);
  });
  render.join();
  return result;
}
static BookHandle* mappingBook;
static unsigned mappingSectionOpens;
static void exercise(void* section, void* raw) {
  auto& handle = book(raw);
  const XpEpubRenderSpec spec{16, 1.0f, 240, 320, 0, 0, 0, 100, 0, 0, 1, 0, 0};
  assert(handle.value().getCssParser() == nullptr);
  assert(handle.book.use_count() == 1);
  unsigned ticks = 0;
  uint32_t facts[4]{};
  assert(xp_epub_test_section_fixture_start(section, &spec, nullptr, nullptr));
  xp_epub_test_section_fixture_facts(section, &facts);
  while (facts[1]) {
    assert(handle.book.use_count() == 1);
    assert(handle.value().getCssParser() == nullptr);
    assert(buildOnRenderThread(section));
    ++ticks;
    assert(ticks < 5000);
    {
      std::lock_guard<std::mutex> lock(renderMutex);
      assert(handle.value().getSpineItem(0).tocIndex == 0);
      assert(handle.value().getTocItem(0).anchor == "anchor");
      xp_epub_test_section_fixture_facts(section, &facts);
    }
  }
  assert(ticks > 2 && facts[0] > 2 && facts[2]);
  const auto completedPages = static_cast<uint16_t>(facts[0]);
  assert(xp_epub_test_section_fixture_load(section, &spec));
  {
    std::unique_ptr<Page> page(static_cast<Page*>(xp_epub_test_section_fixture_page(section, 0)));
    assert(page && page->elementCount() > 0);
  }
  mappingBook = &handle;
  mappingSectionOpens = 0;
  HalFileTest::allowOpen = [](const char* path, const char*) {
    if (std::string(path).ends_with("/sections/0.bin")) {
      assert(mappingBook->book.use_count() >= 1);
      ++mappingSectionOpens;
    }
    return true;
  };
  CrossPointPosition position{0, 0, completedPages};
  position.visibleTextOffset = 10;
  position.hasVisibleTextOffset = true;
  const auto saved = ProgressMapper::toSavedProgress(handle.book, position);
  assert(!saved.xpath.empty());
  const auto restored = ProgressMapper::toCrossPoint(handle.book, saved, driverRenderer, 0, completedPages);
  assert(restored.spineIndex == 0 && restored.hasVisibleTextOffset && restored.visibleTextOffset == 10);
  const auto xpath = ChapterXPathResolver::findXPathForParagraph(handle.book, 0, 2);
  assert(!xpath.empty());
  assert(mappingSectionOpens > 0 && handle.book.use_count() == 1);
  HalFileTest::allowOpen = nullptr;
  mappingBook = nullptr;
  assert(xp_epub_test_section_fixture_start(section, &spec, nullptr, nullptr));
  for (int i = 0; i < 2; ++i) {
    assert(buildOnRenderThread(section));
    xp_epub_test_section_fixture_facts(section, &facts);
    assert(facts[1]);
    assert(handle.book.use_count() == 1 && handle.value().getCssParser() == nullptr);
  }
  std::printf(
      "actual Section/Chapter parser: %u ticks, native Page and facade metadata reads, "
      "mapper cache opens=%u; active parser closes before Book.\n",
      ticks, mappingSectionOpens);
}
int main(int argc, char** argv) {
  assert(argc == 3);
  std::string path = argv[1], cache = argv[2];
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
  auto* owner = xp_epub_test_section_fixture_open(&resources, &leaves, &runtime, text(path), text(cache), 0);
  assert(owner);
  auto* facade = xp_epub_test_section_fixture_facade(owner);
  assert(facade);
  exercise(owner, facade);
  xp_epub_test_section_fixture_close(owner);
  assert(driverRenderer.loanDepth == 0);
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
}
