#include "EpubReaderChapterSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UIScale.h"
#include "components/UITheme.h"

EpubReaderChapterSelectionActivity::EpubReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const std::shared_ptr<Epub>& epub,
                                                                       const int currentTocIndex)
    : ReaderChromeListActivity("EpubReaderChapterSelection", renderer, mappedInput, XP_UI_READER_CHAPTERS, TOC_WINDOW),
      epub(epub),
      currentTocIndex(currentTocIndex) {}

void EpubReaderChapterSelectionActivity::onEnter() {
  ReaderChromeListActivity::onEnter();
  if (!resourcesReady()) return;

  // The reader underneath pins its page-render glyph arenas while this
  // overlay is up. clearCache() is heap-adaptive: below the retention floor
  // it frees them (the next page render's PrewarmScope rebuilds them at
  // normal page-turn cost), giving this list room to keep every row's
  // fallback glyphs resident — otherwise each repaint re-reads the visible
  // rows' glyphs from SD.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  if (!epub) {
    return;
  }

  // Rust follows the current chapter into the first visible window.
  selectIndex(currentTocIndex);
}

// Materialize the label window starting at `start` (clamped). TOC
// entries are SD LUT reads (getTocItem), so this runs only when the viewport
// leaves the current window. Finishes with a batch prewarm of the window's
// CJK fallback glyphs -- one bounded SD pass per list page; repaints inside
// the window stay RAM-only.
void EpubReaderChapterSelectionActivity::refreshTocWindow(const int start) {
  const int total = listCount();
  int clamped = start;
  if (clamped > total - TOC_WINDOW) clamped = total - TOC_WINDOW;
  if (clamped < 0) clamped = 0;
  if (clamped == windowStart) return;

  windowCount = total - clamped < TOC_WINDOW ? total - clamped : TOC_WINDOW;
  for (int i = 0; i < windowCount; i++) {
    const auto tocItem = epub->getTocItem(clamped + i);
    std::string indent(tocItem.level > 0 ? (tocItem.level - 1) * 2 : 0, ' ');
    windowLabels[i] = indent + tocItem.title;
    ReaderChromeRow item;
    item.label = windowLabels[i].c_str();

    windowItems[i] = item;
  }
  windowStart = clamped;

  struct PrewarmCtx {
    const std::string* labels;
    int count;
  } prewarmCtx{windowLabels, windowCount};
  renderer.prewarmFallbackText(
      uiScaleSpec().bodyFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < static_cast<uint32_t>(c->count) ? c->labels[i].c_str() : nullptr;
      },
      &prewarmCtx, static_cast<uint32_t>(windowCount));
}

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) {
    return;
  }
  selectIndex(index);
  const auto tocItem = epub->getTocItem(index);
  if (tocItem.spineIndex == -1) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
  } else {
    setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
    finish();
  }
}

const ReaderChromeRow* EpubReaderChapterSelectionActivity::rowAt(int index) const {
  return index >= windowStart && index < windowStart + windowCount ? &windowItems[index - windowStart] : nullptr;
}
const char* EpubReaderChapterSelectionActivity::headerTitle() const { return tr(STR_SELECT_CHAPTER); }
const char* EpubReaderChapterSelectionActivity::emptyText() const { return epub ? tr(STR_NO_CHAPTERS) : ""; }
