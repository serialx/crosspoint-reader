#include "EpubReaderClippingListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "ClippingStore.h"
#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "clippings/ClippingPreview.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

EpubReaderClippingListActivity::EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : ReaderChromeListActivity("EpubReaderClippings", renderer, mappedInput, XP_UI_READER_CLIPPINGS, ROW_WINDOW) {}

void EpubReaderClippingListActivity::onEnter() {
  ReaderChromeListActivity::onEnter();
  if (!resourcesReady()) return;
  initialListRender = true;
  rebuildRows();
}

int EpubReaderClippingListActivity::listCount() const { return static_cast<int>(CLIPPINGS.clippingCount()); }

const char* EpubReaderClippingListActivity::headerTitle() const { return tr(STR_CLIPPINGS); }

void EpubReaderClippingListActivity::rebuildRows() {
  windowStart = -1;
  windowCount = 0;
}

void EpubReaderClippingListActivity::refreshRowWindow(const int start) {
  const int total = listCount();
  int clamped = std::max(0, start);
  if (clamped > total - ROW_WINDOW) clamped = std::max(0, total - ROW_WINDOW);
  if (clamped == windowStart) return;

  windowCount = std::min(ROW_WINDOW, total - clamped);
  for (int slot = 0; slot < windowCount; ++slot) {
    const size_t clippingIndex = static_cast<size_t>(clamped + slot);
    const Clipping* clipping = CLIPPINGS.clippingAt(clippingIndex);
    previewLengths[slot] = 0;
    previews[slot][0] = '\0';
    if (!clipping || !CLIPPINGS.readClippingPreview(clippingIndex, previews[slot].data(), previews[slot].size(),
                                                    previewLengths[slot])) {
      LOG_ERR("CLIP", "Failed to read clipping %u", static_cast<unsigned>(clippingIndex));
    }

    ReaderChromeRow item;
    item.label = clipping && clipping->chapterTitle[0] != '\0' ? clipping->chapterTitle : tr(STR_CLIPPINGS);
    item.subtitle = previews[slot].data();
    item.bookmarkIcon = true;

    rowItems[slot] = item;
  }
  windowStart = clamped;
}

void EpubReaderClippingListActivity::openSelected() {
  if (confirmPopup.isActive() || selectedIndex() < 0 || selectedIndex() >= listCount()) return;
  const Clipping* clipping = CLIPPINGS.clippingAt(static_cast<size_t>(selectedIndex()));
  if (!clipping) return;
  static constexpr StrId OPTIONS[] = {StrId::STR_OPEN, StrId::STR_GO_TO_PASSAGE};
  const bool anchored = clipping->startOffset != UINT32_MAX && clipping->endOffset != UINT32_MAX;
  confirmPopup.show(StrId::STR_CLIPPINGS, OPTIONS, anchored ? 2 : 1, 0, [this](const int index) {
    if (index == 0) {
      readSelected();
      return;
    }
    const Clipping* selectedClipping = CLIPPINGS.clippingAt(static_cast<size_t>(selectedIndex()));
    if (!selectedClipping) return;
    ProgressChangeResult result;
    result.spineIndex = selectedClipping->spineIndex;
    result.page = selectedClipping->startPage;
    result.hasVisibleTextOffset = true;
    result.visibleTextOffset = selectedClipping->startOffset;
    setResult(std::move(result));
    finish();
  });
  requestUpdate();
}

void EpubReaderClippingListActivity::readSelected() {
  if (selectedIndex() < 0 || selectedIndex() >= listCount()) return;
  const Clipping* clipping = CLIPPINGS.clippingAt(static_cast<size_t>(selectedIndex()));
  std::string text;
  if (!clipping || !CLIPPINGS.readClippingText(static_cast<size_t>(selectedIndex()), text)) return;
  const std::string title = clipping->chapterTitle[0] != '\0' ? clipping->chapterTitle : tr(STR_CLIPPINGS);
  auto detail = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, title, std::move(text));
  if (!detail) {
    LOG_ERR("CLIP", "Failed to allocate clipping detail activity");
    return;
  }
  startActivityForResult(std::move(detail), [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderClippingListActivity::activateIndex(const int index) {
  if (confirmPopup.isActive() || index < 0 || index >= listCount()) return;
  selectIndex(index);
  openSelected();
}

void EpubReaderClippingListActivity::onRowLongPress(const int index) {
  if (confirmPopup.isActive() || index < 0 || index >= listCount()) return;
  selectIndex(index);
  showDeleteConfirmation();
}

void EpubReaderClippingListActivity::showDeleteConfirmation() {
  if (listCount() == 0 || confirmPopup.isActive()) return;
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_DELETE_CLIPPING_CONFIRM), options, 2, 0, [this](const int index) {
    confirmingDelete = false;
    if (index == 1) deleteSelected();
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderClippingListActivity::deleteSelected() {
  RenderLock lock(*this);
  if (selectedIndex() < 0 || selectedIndex() >= listCount()) return;
  if (!CLIPPINGS.removeClippingAt(static_cast<size_t>(selectedIndex()))) {
    LOG_ERR("CLIP", "Failed to delete clipping %d", static_cast<int>(selectedIndex()));
    return;
  }
  rebuildRows();
  if (listCount() == 0) {
    finish();
    return;
  }
  if (selectedIndex() >= listCount()) selectIndex(listCount() - 1);
  selectIndex(selectedIndex());
}

bool EpubReaderClippingListActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  return false;
}

void EpubReaderClippingListActivity::render(RenderLock&& lock) {
  if (confirmPopup.processRender(renderer, mappedInput)) return;
  if (initialListRender && listCount() > 0) {
    renderer.clearScreen();
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  }
  initialListRender = false;
  renderList();
}

const ReaderChromeRow* EpubReaderClippingListActivity::rowAt(int index) const {
  return index >= windowStart && index < windowStart + windowCount ? &rowItems[index - windowStart] : nullptr;
}
