extern "C" void xp_epub_test_popup_size_fault(uint8_t);
int main(int argc, char** argv) {
  assert(argc == 2);
  const auto fault = static_cast<uint8_t>(std::stoul(argv[1]));
  for (bool partial : {false, true}) {
    ticks = 0;
    Section::seed = {};
    SdCardFont::notice = false;
    EpubReaderActivity a;
    a.currentSpineIndex = 1;
    // The healthy control uses the existing no-byte-estimate popup policy.
    if (!fault) a.epub->bytes = 0;
    auto& cfg = Section::seed;
    cfg.cached = partial;
    cfg.partial = partial;
    cfg.count = partial ? 2 : 0;
    cfg.finalCount = 10;
    cfg.complete = false;
    cfg.offset = partial;
    cfg.html = false;
    if (partial) {
      a.pendingOffsetJump = 123;
      a.nextPageNumber = 8;
    }
    xp_epub_test_popup_size_fault(fault);
    std::printf("case partial=%d\n", partial);
    a.renderBook();
    a.observeOwner();
    a.dump();
    assert(a.nativeSection && a.nativeSection->pageCount > 0);
    assert(a.lastSavedSpineIndex == 1);
    assert(a.lastSavedPage >= 0 && a.lastSavedPageCount > 0);
    assert(!a.pendingOffsetJump);
  }
}
