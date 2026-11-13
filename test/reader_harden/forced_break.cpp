
extern "C" void xp_test_fail_allocation_after(size_t);
static size_t rowTocOffset, reads, faults;
int main(int argc, char** argv) {
  assert(argc == 6);
  std::string path = argv[1], cache = argv[2], mode = argv[3];
  rowTocOffset = std::stoul(argv[4]);
  sectionMetrics.context = &driverRenderer;
  {
    CssParser css(cache);
    HalFile f;
    assert(f.open((cache + "/source.css").c_str(), "rb"));
    assert(css.loadFromStream(f) == CssParser::ParseResult::Complete);
    assert(css.saveToCache(true));
  }
  static const XpAppRuntime runtime = [] {
    auto v = unexpectedAppRuntime();
    v.now_ms = []() -> uint32_t { return 0; };
    v.delay_ms = [](uint32_t) {};
    return v;
  }();
  void* owner = xp_epub_test_section_fixture_open(&resources, &leaves, &runtime, text(path), text(cache), 0);
  assert(owner);
  if (mode == "M2-oom")
    HalFileTest::readLimit = [](const char* path, size_t at, size_t n) -> size_t {
      if (std::string(path).ends_with("/book.bin") && at == rowTocOffset && ++reads == 1) {
        ++faults;
        xp_test_fail_allocation_after(0);
      }
      return n;
    };
  if (mode == "M2")
    HalFileTest::readLimit = [](const char* path, size_t at, size_t n) -> size_t {
      if (std::string(path).ends_with("/book.bin") && at == rowTocOffset && ++reads == 2) {
        ++faults;
        return 0;
      }
      return n;
    };
  const XpEpubRenderSpec spec{16, 1.0f, 240, 320, 0, 0, 0, 100, 0, 0, 1, 0, 0};
  const bool started = xp_epub_test_section_fixture_start(owner, &spec, nullptr, nullptr);
  xp_test_fail_allocation_after(SIZE_MAX);
  uint32_t facts[4]{};
  xp_epub_test_section_fixture_facts(owner, &facts);
  unsigned ticks = 0;
  while (started && facts[1]) {
    if (!xp_epub_test_section_fixture_advance(owner, 1, 0)) break;
    assert(++ticks < 5000);
    xp_epub_test_section_fixture_facts(owner, &facts);
  }
  HalFileTest::readLimit = nullptr;
  printf("CP0 mode=%s started=%d complete=%u pages=%u faults=%zu ticks=%u\n", mode.c_str(), started, facts[2], facts[0],
         faults, ticks);
  xp_epub_test_section_fixture_close(owner);
  assert(driverRenderer.loanDepth == 0 && xp_test_live_allocations() == 0 && nativeBuffers.empty());
  if (mode != "control") {
    assert(faults == 1 && !started && !facts[2]);
    assert(!std::filesystem::exists(cache + "/sections/0.bin"));
    assert(!std::filesystem::exists(cache + "/sections/0.bin.part"));
  } else {
    assert(started && facts[2] && facts[0] == 3);
  }
  return 0;
}
