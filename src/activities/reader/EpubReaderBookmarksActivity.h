#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "../../BookmarkEntry.h"
#include "ReaderChromeListActivity.h"
#include "components/OptionPopup.h"

class EpubReaderBookmarksActivity final : public ReaderChromeListActivity {
  // The list rides the ReaderChromeListActivity scaffold (themed rows, touch routing);
  // the title keeps its legacy draw, and OptionPopup keeps its legacy overlay
  // rendering for the delete confirmation.
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  std::vector<BookmarkEntry> bookmarks;
  // Row buffers derived from `bookmarks`, rebuilt only when it changes
  // (onEnter() load, post-delete) instead of on every repaint — buildScreen()
  // used to re-compose a percentage/chapter/TOC-title subtitle string per
  // bookmark on every render (cursor move, tap flash, ...).
  std::vector<std::string> bookmarkSubtitles;
  std::vector<ReaderChromeRow> bookmarkRowItems;
  void rebuildBookmarkRowItems();
  bool confirmingDelete = false;
  OptionPopup confirmPopup;

 public:
  explicit EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::shared_ptr<Epub>& epub, const std::string& epubPath);
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(bookmarks.size()); }
  const ReaderChromeRow* rowAt(int index) const override;
  const char* headerTitle() const override { return tr(STR_BOOKMARKS); }
  const char* emptyText() const override { return tr(STR_NO_BOOKMARKS); }
  const char* helpText() const override { return tr(STR_HOLD_OPEN_FOR_ACTIONS); }
  void drawFooter() override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
#if FREEINK_CAP_TOUCH

  void swipeDelete(int index) override;
#endif
  // Popup handling runs before everything else each pass.
  bool handleCustomInput() override;
  // Back cancels with a result; Confirm opens on release and a hold shows actions.

  // Open the selected bookmark: finishes with a ProgressChangeResult for the reader.
  void openSelectedBookmark();

  void startRename();
  void showBookmarkActions();

  // Opens the Cancel/Delete confirmation for the selected bookmark.
  void showDeleteConfirmation();

  // Delete the currently selected bookmark and persist the list
  void deleteSelectedBookmark();
};
