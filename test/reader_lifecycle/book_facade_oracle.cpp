#undef malloc
#undef free
extern "C" uint8_t xp_epub_test_facade(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                       uint8_t (*)(void*));
extern "C" size_t xp_test_live_allocations();
static std::string destination;
static unsigned facadeFailure, facadeAttempt, facadeInvocations;
static bool allocatingFacade;
static std::string forbiddenGetter;
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  if (allocatingFacade && ++facadeAttempt == facadeFailure) return nullptr;
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
static XpEpubLeaves checkedLeaves = [] {
  auto value = leaves;
  value.book_facade_open = [](void* owner, const XpEpubBookView* view, const XpEpubBookBorrow* callbacks) -> void* {
    allocatingFacade = true;
    auto* result = leaves.book_facade_open(owner, view, callbacks);
    allocatingFacade = false;
    return result;
  };
  return value;
}();
struct NestedOutput : Print {
  std::shared_ptr<Epub> epub;
  std::string bytes;
  explicit NestedOutput(std::shared_ptr<Epub> value) : epub(std::move(value)) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    size_t size = 0;
    assert(epub->getItemSize("b.xhtml", &size) && size == 6);
    assert(epub->getSpineItem(1).cumulativeSize == 12);
    assert(epub->getTocItem(0).href == "a.xhtml");
    bytes.append(reinterpret_cast<const char*>(p), n);
    return n;
  }
};
static uint8_t invoke(void* raw) {
  ++facadeInvocations;
  auto& h = book(raw);
  assert(h.book.use_count() == 1);
  assert(ImageBlock::extractCtx == h.book.get());
  if (forbiddenGetter == "path") (void)h.value().getPath();
  if (forbiddenGetter == "title") (void)h.value().getTitle();
  if (forbiddenGetter == "author") (void)h.value().getAuthor();
  assert(h.value().getLanguage() == "en" && h.value().getSpineItemsCount() == 2 && h.value().getTocItemsCount() == 1);
  assert(h.value().getBookSize() == 12 && h.value().getSpineIndexForSize(8) == 1);
  assert(h.value().getSpineItem(0).tocIndex == 0 && h.value().getTocItem(0).anchor == "anchor");
  assert(h.value().calculateProgress(1, .5f) == .75f);
  auto* sectionCss = h.value().getCssParser();
  assert(sectionCss && sectionCss->empty());
  assert(sectionCss->loadFromCache() == CssParser::CacheLoadResult::Complete);
  for (int tick = 0; tick < 2; ++tick) {
    auto sectionLoan = h.book;
    assert(h.book.use_count() == 2);
    auto* css = sectionLoan->getCssParser();
    assert(css && css->ruleCount() == 1);
    assert(css->resolveStyle("p", "").hasMarginTop());
    NestedOutput output(sectionLoan);
    assert(sectionLoan->readItemContentsToStream("a.xhtml", output, 1024));
    assert(output.bytes == "first!");
  }
  sectionCss->clear();
  assert(sectionCss->empty());
  assert(h.book.use_count() == 1);
  assert(ImageBlock::extractFn(ImageBlock::extractCtx, "b.xhtml", destination.c_str()));
  leaves.book_set_extractor(nullptr);
  assert(!ImageBlock::extractCtx && !ImageBlock::extractFn);
  leaves.book_set_extractor(raw);
  assert(ImageBlock::extractCtx == h.book.get());
  leaves.book_set_extractor(nullptr);
  return 1;
}
int main(int argc, char** argv) {
  assert(argc == 4 || argc == 5);
  std::string source = argv[1], cache = argv[2];
  destination = argv[3];
  if (argc == 5) forbiddenGetter = argv[4];
  {
    CssParser css(cache);
    HalFile f;
    assert(f.open((cache + "/source.css").c_str(), "rb"));
    assert(css.loadFromStream(f) == CssParser::ParseResult::Complete);
    assert(css.saveToCache(true));
  }
  for (unsigned fail = 1; fail <= 2; ++fail) {
    facadeFailure = fail;
    facadeAttempt = 0;
    assert(xp_epub_test_facade(&resources, &checkedLeaves, text(source), text(cache), invoke) == 0);
    assert(facadeAttempt == fail && facadeInvocations == 0);
    assert(!ImageBlock::extractCtx && !ImageBlock::extractFn);
    assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
  }
  facadeFailure = 0;
  facadeAttempt = 0;
  assert(xp_epub_test_facade(&resources, &checkedLeaves, text(source), text(cache), invoke) == 1);
  assert(facadeAttempt == 2 && facadeInvocations == 1);
  assert(!ImageBlock::extractCtx && !ImageBlock::extractFn);
  assert(xp_test_live_allocations() == 0);
  assert(nativeBuffers.empty());
  puts(
      "actual Rust owner/native Epub facade: rows, nested stream/image, CSS ticks, extractor detach/rebind and two "
      "checked allocation failures passed");
}
