int main() {
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
    assert(!a.nativeSection->config.building && a.partialRebuildStartFailed);
    assert(a.lastSavedSpineIndex == 0 && a.lastSavedPage == 2 && a.lastSavedPageCount == 10);
  }
  std::puts("PASS: failed new chapter and partial extension recover saved page; no eager build or false progress save");
}
