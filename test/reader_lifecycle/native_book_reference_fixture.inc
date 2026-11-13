// Native reference adapter for historical comparison fixtures only.
struct BookHandle {
  std::shared_ptr<Epub> book;

  bool closing = false;
  Epub& value() const {
    assert(!closing);
    return *book;
  }
  void bindExtractor() {
    assert(!closing);
    ReferenceImage::setExtractor(book.get(), [](void* raw, const char* source, const char* destination) {
      return static_cast<Epub*>(raw)->extractItemToFile(source, destination);
    });
  }
};
BookHandle& book(void* handle) { return *static_cast<BookHandle*>(handle); }
static const auto referenceOpen = [](void* owner, const XpEpubBookView* view, const XpEpubBookBorrow* borrow) -> void* {
  auto handle = makeUniqueNoThrow<BookHandle>();
  if (!handle) return nullptr;
  auto facade = makeUniqueNoThrow<Epub>(owner, *view, *borrow);
  if (!facade) return nullptr;
  handle->book = std::move(facade);
  ReferenceImage::clearRenderFailures();
  handle->bindExtractor();
  return handle.release();
};
static const auto referenceClose = [](void* handle) {
  auto& facade = book(handle);
  assert(!facade.closing && facade.book.use_count() == 1);
  facade.closing = true;
  delete &facade;
};
static const auto referenceExtractor = [](void* handle) {
  if (handle)
    book(handle).bindExtractor();
  else
    ReferenceImage::setExtractor(nullptr, nullptr);
};
extern "C" void xp_epub_test_native_reference_bind(void* (*)(void*, const XpEpubBookView*, const XpEpubBookBorrow*),
                                                   void (*)(void*), void (*)(void*));
static const bool referenceBound = [] {
  xp_epub_test_native_reference_bind(referenceOpen, referenceClose, referenceExtractor);
  return true;
}();
