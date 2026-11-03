#undef malloc
#undef free
extern "C" uint8_t xp_epub_test_facade(const XpAppResources*, const XpEpubLeaves*, XpAppText, XpAppText,
                                       uint8_t (*)(void*));
extern "C" size_t xp_test_live_allocations();
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_allocation_attempts();
static void* owner;
static const XpEpubBookBorrow* borrow;
static std::string expected;
static uint8_t useTitle(void*, const XpEpubToc* row) {
  assert(std::string(reinterpret_cast<const char*>(row->title.data), row->title.len) == expected);
  BookTextUi::checkToolbar(row->title, expected);
  BookTextAdapters::verify(row->title, expected);
  return 1;
}
static uint8_t invokeText(void*) {
  const auto before = xp_test_live_allocations();
  xp_test_fail_allocation_after(0);
  assert(borrow->toc(
      owner, 0,
      [](void*, const XpEpubToc* row) -> uint8_t {
        assert(row->title.len == 0 && std::string(pathText(row->title)).empty());
        return 1;
      },
      nullptr));
  assert(xp_test_allocation_attempts() == 1 && xp_test_live_allocations() == before);
  xp_test_fail_allocation_after(SIZE_MAX);
  return borrow->toc(owner, 0, useTitle, nullptr);
}
int main(int argc, char** argv) {
  if (argc == 2) {
    uint8_t source[2]{'x', '!'};
    XpAppText invalid{source, 1};
    if (std::string(argv[1]) == "bad-epub")
      pathText(invalid);
    else
      cstr(invalid);
    assert(false && "unterminated span was accepted");
  }
  assert(argc == 4);
  std::string source = argv[1], cache = argv[2];
  FILE* input = std::fopen(argv[3], "rb");
  assert(input);
  for (int c; (c = std::fgetc(input)) != EOF;) expected.push_back(char(c));
  std::fclose(input);
  auto actual = leaves;
  actual.book_facade_open = [](void* raw, const XpEpubBookView* view, const XpEpubBookBorrow* callbacks) -> void* {
    owner = raw;
    borrow = callbacks;
    return leaves.book_facade_open(raw, view, callbacks);
  };
  assert(xp_epub_test_facade(&resources, &actual, text(source), text(cache), invokeText));
  assert(xp_test_live_allocations() == 0 && nativeBuffers.empty());
  std::puts("actual cache title -> native toolbar -> SDK: exact C-string prefix passed");
}
