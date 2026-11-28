extern "C" uint8_t xp_epub_test_book_turn(void*, uint8_t);
int main(int argc, char**) {
  if (argc == 1)
    for (bool partial : {false, true}) {
      ticks = 0;
      Section::seed = {};
      SdCardFont::notice = false;
      EpubReaderActivity a;
      a.currentSpineIndex = partial ? 0 : 1;
      a.nextPageNumber = 9;
      a.lastSavedSpineIndex = 0;
      a.lastSavedPage = 2;
      a.lastSavedPageCount = 10;
      a.cachedSpineIndex = 0;
      a.cachedChapterTotalPageCount = 10;
      a.pendingOffsetJump = 123;
      a.automaticPageTurnActive = true;
      Section::seed.cached = partial;
      Section::seed.partial = partial;
      Section::seed.complete = false;
      Section::seed.count = partial ? 5 : 0;
      Section::seed.startOk = false;
      a.renderBook();
      a.observeOwner();
      assert(!a.nativeSection && a.currentSpineIndex == 0 && a.nextPageNumber == 2);
      assert(a.lastSavedSpineIndex == 0 && a.lastSavedPage == 2 && a.lastSavedPageCount == 10);
      assert(!a.pendingOffsetJump && !a.automaticPageTurnActive && a.partialRebuildStartFailed);
      // The explicit next render reopens the saved cache. A partial extension still fails.
      Section::seed.cached = true;
      Section::seed.partial = partial;
      Section::seed.complete = !partial;
      Section::seed.count = partial ? 5 : 10;
      a.renderBook();
      a.observeOwner();
      assert(a.nativeSection && a.nativeSection->currentPage == 2 && a.currentSpineIndex == 0);
      assert(!a.nativeSection->config.building && a.partialRebuildStartFailed == partial);
      assert(a.lastSavedSpineIndex == 0 && a.lastSavedPage == 2 && a.lastSavedPageCount == 10);
    }
  for (bool recovered : {false, true}) {
    ticks = 0;
    Section::seed = {};
    EpubReaderActivity a;
    a.currentSpineIndex = 0;
    a.nextPageNumber = 9;
    a.lastSavedSpineIndex = 0;
    a.lastSavedPage = 2;
    a.lastSavedPageCount = 10;
    Section::seed.cached = true;
    Section::seed.partial = true;
    Section::seed.complete = false;
    Section::seed.count = 5;
    Section::seed.startOk = false;
    a.renderBook();
    a.observeOwner();
    assert(!a.nativeSection && a.nextPageNumber == 2 && a.partialRebuildStartFailed);
    // Previous in the loop requests this render; it does not decrement the restored destination.
    a.renderBook();
    a.observeOwner();
    assert(a.nativeSection && a.nativeSection->currentPage == 2 && a.partialRebuildStartFailed);
    a.nativeSection->config.startOk = recovered;
    for (int page = 3; page <= 5; ++page) {
      {
        book_render_adapter::Entry entry(a);
        assert(xp_epub_test_book_turn(a.rustOwner, 1));
      }
      a.observeOwner();
      assert(a.currentSpineIndex == 0 && a.nativeSection->currentPage == page);
      a.renderBook();
      a.observeOwner();
      if (page < 5) {
        assert(a.nativeSection && a.nativeSection->currentPage == page && a.partialRebuildStartFailed);
      } else if (recovered) {
        assert(a.nativeSection && a.nativeSection->currentPage == 5 && !a.partialRebuildStartFailed);
        assert(a.currentSpineIndex == 0 && a.lastSavedSpineIndex == 0 && a.lastSavedPage == 5);
      } else {
        assert(!a.nativeSection && a.currentSpineIndex == 0 && a.nextPageNumber == 4);
        assert(a.lastSavedSpineIndex == 0 && a.lastSavedPage == 4 && a.partialRebuildStartFailed);
      }
    }
  }
  std::puts("PASS: recovered watermark extends within chapter; persistent failure restores page 4 instead of skipping");
}
