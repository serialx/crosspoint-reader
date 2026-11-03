#include <mutex>
#include <thread>
#undef malloc
#undef free
extern "C" uint8_t xp_epub_test_facade(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                       uint8_t (*)(void*));
extern "C" size_t xp_test_live_allocations();
static std::mutex renderMutex;
static bool buildOnRenderThread(Section& section) {
  bool result = false;
  std::thread render([&] {
    std::lock_guard<std::mutex> lock(renderMutex);
    result = section.buildSomeMore(1);
  });
  render.join();
  return result;
}
static BookHandle* mappingBook;
static unsigned mappingSectionOpens;
static uint8_t exercise(void* raw) {
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
  auto* css = handle.value().getCssParser();
  assert(css->empty());
  unsigned ticks = 0;
  int completedPages = 0;
  {
    Section section(handle.book, 0, renderer);
    assert(section.startBuild(spec));
    assert(css->ruleCount() == 1);
    while (section.isBuilding()) {
      assert(handle.book.use_count() >= 3);
      assert(css->ruleCount() == 1);
      assert(buildOnRenderThread(section));
      ++ticks;
      assert(ticks < 5000);
      {
        std::lock_guard<std::mutex> lock(renderMutex);
        assert(handle.value().getSpineItem(0).tocIndex == 0);
        assert(handle.value().getTocItem(0).anchor == "anchor");
      }
    }
    assert(ticks > 2 && section.pageCount > 2 && css->empty());
    completedPages = section.pageCount;
    assert(section.loadSectionFile(spec));
    assert(section.loadPage(0));
  }
  assert(handle.book.use_count() == 1);
  mappingBook = &handle;
  mappingSectionOpens = 0;
  HalFileTest::allowOpen = [](const char* path, const char*) {
    if (std::string(path).ends_with("/sections/0.bin")) {
      assert(mappingBook->book.use_count() >= 2);
      ++mappingSectionOpens;
    }
    return true;
  };
  CrossPointPosition position{0, 0, completedPages};
  position.visibleTextOffset = 10;
  position.hasVisibleTextOffset = true;
  const auto saved = ProgressMapper::toSavedProgress(handle.book, position);
  assert(!saved.xpath.empty());
  const auto restored = ProgressMapper::toCrossPoint(handle.book, saved, renderer, 0, completedPages);
  assert(restored.spineIndex == 0 && restored.hasVisibleTextOffset && restored.visibleTextOffset == 10);
  const auto xpath = ChapterXPathResolver::findXPathForParagraph(handle.book, 0, 2);
  assert(!xpath.empty());
  assert(mappingSectionOpens > 0 && handle.book.use_count() == 1);
  HalFileTest::allowOpen = nullptr;
  mappingBook = nullptr;
  {
    Section section(handle.book, 0, renderer);
    assert(section.startBuild(spec));
    assert(buildOnRenderThread(section));
    assert(section.isBuilding());
    assert(buildOnRenderThread(section));
    assert(section.isBuilding());
    assert(css->ruleCount() == 1 && handle.book.use_count() >= 3);
  }
  assert(handle.book.use_count() == 1);
  leaves.book_set_extractor(nullptr);
  std::printf(
      "actual Section/Chapter parser: %u ticks, completed cache read, active parser abort released facade loans; "
      "mapper temporary Section opens=%u\n",
      ticks, mappingSectionOpens);
  return 1;
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
  assert(xp_epub_test_facade(&resources, &leaves, text(path), text(cache), exercise));
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
}
