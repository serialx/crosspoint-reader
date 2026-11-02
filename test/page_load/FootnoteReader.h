#pragma once
#include <Epub/Page.h>

#include <functional>

#include "activities/Activity.h"
class CheckedPageMetadata : public PageMetadata {
 public:
  auto footnotes() const {
    RenderLock::requireHeld();
    return PageMetadata::footnotes();
  }
  auto links() const {
    RenderLock::requireHeld();
    return PageMetadata::links();
  }
};
struct FootnoteSection {
  int currentPage = 0;
  int loads = 0;
  std::unique_ptr<Page> loaded;
  std::unique_ptr<Page> loadPage(int) {
    RenderLock::requireHeld();
    ++loads;
    return std::move(loaded);
  }
};
class EpubReaderActivity {
 public:
  FootnoteSection* section;
  CheckedPageMetadata currentPageMetadata;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::string navigatedHref;
  bool isFootnote = false;
  bool atEndOfBook = false;
  int currentPageLinkMarginLeft = 5, currentPageLinkMarginTop = 7;
  Activity* child = nullptr;  // Borrowed from the real App child owner.
  std::function<void(const ActivityResult&)> completion;
  int menuRequests = 0, updates = 0;
  EpubReaderActivity(FootnoteSection&, GfxRenderer&, MappedInputManager&);
  ~EpubReaderActivity();
  EpubReaderActivity(const EpubReaderActivity&) = delete;
  EpubReaderActivity& operator=(const EpubReaderActivity&) = delete;
  void preparePage();
  void openFootnoteSelect(bool reopenMenuOnCancel);

  void testLinkTap();
  bool testMenuAvailability();
  bool testMoreAvailability();
};
