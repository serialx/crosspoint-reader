#include "EpubReaderBookmarksActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "../../util/BookmarkFile.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

EpubReaderBookmarksActivity::EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const std::shared_ptr<Epub>& epub, const std::string& epubPath)
    : ReaderChromeListActivity("EpubReaderBookmarks", renderer, mappedInput, XP_UI_READER_BOOKMARKS, 24),
      epub(epub),
      epubPath(epubPath) {}

void EpubReaderBookmarksActivity::onEnter() {
  ReaderChromeListActivity::onEnter();
  if (!resourcesReady()) return;

  if (!epub) {
    return;
  }

  BookmarkFile::load(epubPath, bookmarks);  // clears `bookmarks` first; a missing file leaves it empty
  LOG_DBG("EPB", "Loaded %d bookmarks for book: %s", static_cast<int>(bookmarks.size()), epubPath.c_str());
  rebuildBookmarkRowItems();
}

// Derives bookmarkSubtitles/bookmarkRowItems from `bookmarks`. Called
// whenever `bookmarks` changes (onEnter() load, post-delete) so the scene builder
// reuses the cached rows on every repaint instead of re-composing a
// percentage/chapter/TOC-title subtitle string per bookmark each time.
void EpubReaderBookmarksActivity::rebuildBookmarkRowItems() {
  bookmarkSubtitles.clear();
  bookmarkRowItems.clear();
  if (!epub) {
    return;
  }
  bookmarkSubtitles.reserve(bookmarks.size());
  bookmarkRowItems.reserve(bookmarks.size());
  for (const auto& bookmark : bookmarks) {
    const auto tocIndex = epub->getTocIndexForSpineIndex(bookmark.computedSpineIndex);
    const auto tocTitle = (tocIndex >= 0) ? epub->getTocItem(tocIndex).title : tr(STR_UNNAMED);
    std::string subtitle = std::to_string((int)(std::clamp(bookmark.percentage, 0.0f, 1.0f) * 100.0f + 0.5f)) + "% - ";
    if (bookmark.computedChapterPageCount > 0) {
      subtitle += std::to_string(bookmark.computedChapterProgress + 1) + "/" +
                  std::to_string(bookmark.computedChapterPageCount) + " - ";
    }
    subtitle += tocTitle;
    bookmarkSubtitles.push_back(std::move(subtitle));

    ReaderChromeRow item;
    item.label = bookmark.name.empty() ? bookmark.summary.c_str() : bookmark.name.c_str();
    item.subtitle = bookmarkSubtitles.back().c_str();
    item.bookmarkIcon = true;

    bookmarkRowItems.push_back(item);
  }
}

void EpubReaderBookmarksActivity::openSelectedBookmark() {
  if (bookmarks.empty()) {
    return;
  }
  const auto& bookmark = bookmarks.at(selectedIndex());
  ProgressChangeResult result{};
  result.xpath = bookmark.xpath;
  result.percentage = bookmark.percentage;
  result.hasSavedProgress = true;
  result.hasVisibleTextOffset = bookmark.hasVisibleTextOffset;
  result.visibleTextOffset = bookmark.visibleTextOffset;
  // The offset is spine-relative, so carry its spine even when the legacy page hints below
  // are stale. The reader validates the index.
  result.spineIndex = bookmark.computedSpineIndex;
  if (bookmark.computedChapterPageCount > 0 && bookmark.computedChapterProgress < bookmark.computedChapterPageCount &&
      bookmark.computedSpineIndex < epub->getSpineItemsCount()) {
    result.page = bookmark.computedChapterProgress;
    result.totalPages = bookmark.computedChapterPageCount;
  }
  setResult(std::move(result));
  finish();
}

void EpubReaderBookmarksActivity::activateIndex(const int index) {
  if (confirmPopup.isActive()) return;
  // The interaction table can deliver a row index captured before a delete
  // shrank the list; the next render re-registers the rows.
  if (index < 0 || index >= listCount()) return;
  selectIndex(index);
  openSelectedBookmark();
}

void EpubReaderBookmarksActivity::onRowLongPress(const int index) {
  if (confirmPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  selectIndex(index);
  showBookmarkActions();
}

bool EpubReaderBookmarksActivity::handleCustomInput() {
  // Bookmark action or delete confirmation popup.
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    // Popup dismissed without a selection (Back button or tap outside): cancel delete
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  return false;
}

void EpubReaderBookmarksActivity::startRename() {
  if (bookmarks.empty() || selectedIndex() < 0 || selectedIndex() >= listCount()) {
    return;
  }

  const int renameIndex = selectedIndex();
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_RENAME), bookmarks[renameIndex].name,
                                               BookmarkEntry::MAX_NAME_LENGTH, InputType::Text);
  if (!keyboard) {
    LOG_ERR("EPB", "OOM: bookmark rename keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this, renameIndex](const ActivityResult& result) {
    if (result.isCancelled || renameIndex < 0 || renameIndex >= listCount()) {
      return;
    }
    RenderLock lock(*this);
    std::string previousName = std::move(bookmarks[renameIndex].name);
    bookmarks[renameIndex].name = std::get<KeyboardResult>(result.data).text;
    rebuildBookmarkRowItems();
    if (!BookmarkFile::save(epubPath, bookmarks)) {
      LOG_ERR("EPB", "Failed to save bookmarks after rename");
      bookmarks[renameIndex].name = std::move(previousName);
      rebuildBookmarkRowItems();
    }
    requestUpdate();
  });
}

void EpubReaderBookmarksActivity::showBookmarkActions() {
  if (bookmarks.empty() || confirmPopup.isActive()) {
    return;
  }
  const StrId options[] = {StrId::STR_OPEN, StrId::STR_RENAME, StrId::STR_DELETE};
  confirmPopup.show(StrId::STR_BOOKMARKS, options, 3, 0, [this](const int idx) {
    if (idx == 0) {
      openSelectedBookmark();
    } else if (idx == 1) {
      startRename();
    } else if (idx == 2) {
      showDeleteConfirmation();
    }
  });
  requestUpdate();
}

void EpubReaderBookmarksActivity::showDeleteConfirmation() {
  if (bookmarks.empty() || confirmPopup.isActive()) {
    return;
  }
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_CONFIRM_DELETE_BOOKMARK), options, 2, 0, [this](int idx) {
    confirmingDelete = false;
    if (idx == 1) {
      deleteSelectedBookmark();
    }
    requestUpdate();
  });
  requestUpdate();
}

#if FREEINK_CAP_TOUCH
void EpubReaderBookmarksActivity::swipeDelete(const int index) {
  selectIndex(index);
  showDeleteConfirmation();
}
#endif

void EpubReaderBookmarksActivity::deleteSelectedBookmark() {
  RenderLock lock(*this);
  bookmarks.erase(bookmarks.begin() + selectedIndex());
  // Deleting shifts every later bookmark's index, so the cached subtitles and
  // row pointers must be re-derived, not just trimmed — and before the SD
  // save, so the render task never sees rows aliasing the erased storage.
  rebuildBookmarkRowItems();
  if (!BookmarkFile::save(epubPath, bookmarks)) {
    LOG_ERR("EPB", "Failed to save bookmarks after delete");
  }

  // Move selector up if we deleted the last item
  if (selectedIndex() >= static_cast<int>(bookmarks.size()) && selectedIndex() > 0) {
    selectIndex(selectedIndex() - 1);
  }

  if (bookmarks.empty()) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  selectIndex(selectedIndex());
  requestUpdate(true);
}

const ReaderChromeRow* EpubReaderBookmarksActivity::rowAt(int index) const {
  return index >= 0 && index < listCount() ? &bookmarkRowItems[index] : nullptr;
}
void EpubReaderBookmarksActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), bookmarks.empty() ? "" : tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
void EpubReaderBookmarksActivity::render(RenderLock&&) {
  if (!renderList(false)) return;
  if (confirmPopup.processRender(renderer, mappedInput)) return;
  renderer.displayBuffer();
}
