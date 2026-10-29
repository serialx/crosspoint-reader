#include "EpubReaderActivity.h"

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
#include "ReaderStripPainter.h"
#endif

#include <BlePageTurner.h>
#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>

#include "../../util/BookmarkFile.h"
#include "BookmarkEntry.h"
#include "ChapterBuildGate.h"
#include "ClipSelectionActivity.h"
#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderClippingListActivity.h"
#include "EpubReaderFootnoteSelectActivity.h"
#include "EpubReaderPercentSelectionActivity.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "ProgressMapper.h"
#include "QrDisplayActivity.h"
#include "ReaderActivity.h"
#include "ReaderFontSizes.h"
#include "ReaderToolbarUi.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "SdCardFont.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "clippings/ClippingText.h"
#include "clippings/ClippingsManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookmarkUtil.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

static_assert(static_cast<uint32_t>(HomeButtonAction::Footnotes) == XP_UI_READER_VALUE_HOME_FOOTNOTES);
static_assert(static_cast<uint32_t>(HomeButtonAction::Sync) == XP_UI_READER_VALUE_HOME_SYNC);
static_assert(static_cast<uint32_t>(HomeButtonAction::Bookmark) == XP_UI_READER_VALUE_HOME_BOOKMARK);
static_assert(static_cast<uint32_t>(HomeButtonAction::Dictionary) == XP_UI_READER_VALUE_HOME_DICTIONARY);
static_assert(static_cast<uint32_t>(HomeButtonAction::ReaderMenu) == XP_UI_READER_VALUE_HOME_READER_MENU);
static_assert(static_cast<uint32_t>(HomeButtonAction::CreateClipping) == XP_UI_READER_VALUE_HOME_CREATE_CLIPPING);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_KOSYNC) == XP_UI_READER_VALUE_LONG_MENU_SYNC);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_BOOKMARK) == XP_UI_READER_VALUE_LONG_MENU_BOOKMARK);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_DICTIONARY) == XP_UI_READER_VALUE_LONG_MENU_DICTIONARY);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_CREATE_CLIPPING) ==
              XP_UI_READER_VALUE_LONG_MENU_CREATE_CLIPPING);
static_assert(static_cast<uint32_t>(CrossPointSettings::LP_MENU_FOOTNOTES) == XP_UI_READER_VALUE_LONG_MENU_FOOTNOTES);
static_assert(static_cast<uint32_t>(CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) == XP_UI_READER_VALUE_POWER_FOOTNOTES);
static_assert(static_cast<uint32_t>(CrossPointSettings::SHORT_PWRBTN::CREATE_CLIPPING) ==
              XP_UI_READER_VALUE_POWER_CREATE_CLIPPING);

namespace {
// The X4 Pro and X4 Classic carry the X4's panel but sit outside isXteinkDevice()
// (that helper also gates power management). Overlay refresh choices are per-panel:
// this family runs the grayscale anti-aliasing pass, so chrome painted over a
// fresh page needs the HALF ghost-cleanup and closing re-renders the page.
bool xteinkClassPanel() {
  return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic() || BoardConfig::isEegoA4();
}

constexpr int PAGE_TURN_RATES[] = {1, 1, 3, 6, 12};
constexpr float bookmarkProgressEpsilon = 0.0001f;
constexpr int CLIP_SELECTION_PAGE_WINDOW = 3;

// Word i's font: headings and CSS font-size runs can differ from the section font.
int clippingWordFont(const GfxRenderer& renderer, const int fontId, const TextBlock& block, const uint16_t i) {
  return TextBlock::renderFontId(renderer, block.wordFontId(i, fontId), fontId);
}

Rect clippingWordRect(const GfxRenderer& renderer, const int fontId, const PageElementView& line,
                      const TextBlock& textBlock, const uint16_t i, const int top, const int left,
                      const int wordWidth) {
  const auto* block = &textBlock;
  const int wordFont = clippingWordFont(renderer, fontId, *block, i);
  const char* text = block->wordText(i);
  const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
  const int skipX =
      clippingText::hasEmSpacePrefix(text) ? renderer.getTextAdvanceX(wordFont, "\xe2\x80\x83", style) : 0;
  int width = wordWidth - skipX;
  if (i + 1 < block->wordCount() && block->wordXpos(i + 1) > block->wordXpos(i)) {
    width = std::min(width, static_cast<int>(block->wordXpos(i + 1) - block->wordXpos(i) - skipX));
  }
  const int lineFont = TextBlock::renderFontId(renderer, block->blockFontId(fontId), fontId);
  return Rect{left + line.xPos + block->wordXpos(i) + skipX,
              top + line.yPos + block->getRubyShift(renderer.getFontAscenderSize(lineFont)) +
                  block->wordYOffset(renderer, fontId, i),
              width, renderer.getLineHeight(wordFont)};
}

int clampPercent(int percent) {
  if (percent < 0) {
    return 0;
  }
  if (percent > 100) {
    return 100;
  }
  return percent;
}

constexpr char READ_FOLDER[] = "/read";

bool isInReadFolder(const std::string& path) {
  constexpr size_t n = sizeof(READ_FOLDER) - 1;
  return path.size() > n && path.compare(0, n, READ_FOLDER) == 0 && path[n] == '/';
}

struct ProgressRange {
  float start;
  float end;
};

ProgressRange getPageProgressRange(const std::shared_ptr<Epub>& epub, const int spineIndex, const int page,
                                   const int pageCount) {
  if (pageCount <= 1) {
    return {epub->calculateProgress(spineIndex, 0.0f), epub->calculateProgress(spineIndex, 1.0f)};
  }

  const float step = 1.0f / static_cast<float>(pageCount - 1);
  const float anchor = std::clamp(static_cast<float>(page) * step, 0.0f, 1.0f);
  const float start = std::max(0.0f, anchor - (step * 0.5f));
  const float end = std::min(1.0f, anchor + (step * 0.5f));
  return {epub->calculateProgress(spineIndex, start), epub->calculateProgress(spineIndex, end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  if (bookmark.computedSpineIndex == spineIndex && bookmark.computedChapterPageCount == pageCount &&
      bookmark.computedChapterProgress == page) {
    return true;
  }

  const float bookmarkProgress = std::clamp(bookmark.percentage, 0.0f, 1.0f);
  return bookmarkProgress + bookmarkProgressEpsilon >= pageRange.start &&
         bookmarkProgress - bookmarkProgressEpsilon <= pageRange.end;
}

std::string buildReadFolderDestination(const std::string& srcPath) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  Storage.mkdir(READ_FOLDER);
  std::string dstPath = std::string(READ_FOLDER) + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = std::string(READ_FOLDER) + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

void moveFinishedBookToReadFolder(const std::string& srcPath, const std::string& dstPath,
                                  const std::string& oldCachePath) {
  LOG_INF("ERS", "Moving finished epub: %s -> %s", srcPath.c_str(), dstPath.c_str());
  if (!ClippingStore::moveBook(srcPath, dstPath)) {
    LOG_ERR("ERS", "Failed to move finished book and clippings to '/Read' folder");
    return;
  }

  // Protected-book sidecars travel with the book; a protected book separated
  // from its key no longer opens, so a failed move rolls everything back.
  static constexpr const char* SIDECARS[] = {".key", ".rights"};
  for (size_t i = 0; i < std::size(SIDECARS); i++) {
    const std::string from = srcPath + SIDECARS[i];
    if (!Storage.exists(from.c_str())) continue;
    const std::string to = dstPath + SIDECARS[i];
    if (Storage.rename(from.c_str(), to.c_str())) continue;
    LOG_ERR("ERS", "Failed to move sidecar %s -> %s", from.c_str(), to.c_str());
    for (size_t j = 0; j < i; j++) {
      Storage.rename((dstPath + SIDECARS[j]).c_str(), (srcPath + SIDECARS[j]).c_str());
    }
    if (!Storage.rename(dstPath.c_str(), srcPath.c_str())) {
      LOG_ERR("ERS", "Failed to restore epub after sidecar move failure: %s -> %s", dstPath.c_str(), srcPath.c_str());
    }
    return;
  }

  const std::string newCachePath = "/.crosspoint/epub_" + std::to_string(std::hash<std::string>{}(dstPath));
  bool cacheMoved = false;
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    cacheMoved = Storage.rename(oldCachePath.c_str(), newCachePath.c_str());
    if (!cacheMoved) {
      LOG_ERR("ERS", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(), newCachePath.c_str());
    }
  }

  RECENT_BOOKS.updatePath(srcPath, dstPath, oldCachePath, cacheMoved ? newCachePath : oldCachePath);
  if (APP_STATE.openEpubPath == srcPath) {
    APP_STATE.openEpubPath = dstPath;
    APP_STATE.saveToFile();
  }
}

// A button contact is closed right now. Reads the Xteink ADC ladder and power pin only; on other
// boards a press stops the build once the loop has debounced it.
bool buttonContactClosed() { return gpio.rawInputActive(); }

}  // namespace

EpubReaderActivity::~EpubReaderActivity() {
  ImageBlock::setExtractor(nullptr, nullptr);
  // ActivityManager destroys activities with its RenderLock already held;
  // taking another here self-deadlocks (renderingMutex is non-recursive).
  settleOverlayRefresh();
  discardOverlayPage();  // free the overlay's page snapshot if one is held
  // A radio still stopped for this book's build may start again in the next one.
  if (radioReleasedForBuild.load()) bleturner::afterPaint();

  if (footnoteDepth > 0 && epub) saveLinkStack();

  section.reset();
  CLIPPINGS.unload();
  if (pendingReadFolderMove && epub) {
    const std::string srcPath = epub->getPath();
    const std::string oldCachePath = epub->getCachePath();
    const std::string dstPath = buildReadFolderDestination(srcPath);
    epub.reset();
    moveFinishedBookToReadFolder(srcPath, dstPath, oldCachePath);
  } else {
    epub.reset();
  }
}

bool EpubReaderActivity::loadBook() {
  auto loadedEpub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
  if (!loadedEpub) {
    LOG_ERR("ERS", "Failed to allocate EPUB object");
    return false;
  }

  const bool uncached = !Storage.exists((loadedEpub->getCachePath() + "/book.bin").c_str());
  if (uncached) {
    disableFastInitialRefresh();
    GUI.drawPopup(renderer, tr(STR_INDEXING));
  }

  bool loaded;
  {
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    if (uncached) loan.emplace(renderer);
    loaded = loadedEpub->load(true, SETTINGS.embeddedStyle == 0);
  }
  if (!loaded) {
    // Surfaced by handleLoadFailure() as a dialog; loadedEpub dies with this
    // scope, so carry the reason out in a member.
    loadProtectionError = loadedEpub->getProtectionError();
    LOG_ERR("ERS", "Failed to load EPUB%s%s", loadProtectionError.empty() ? "" : ": ", loadProtectionError.c_str());
    return false;
  }
  epub = std::move(loadedEpub);

  ImageBlock::clearRenderFailures();
  ImageBlock::setExtractor(epub.get(), [](void* ctx, const char* src, const char* dest) {
    return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
  });

  epub->setupCacheDir();

  HalFile f;
  if (Storage.openFileForRead("ERS", epub->getCachePath() + "/progress.bin", f)) {
    uint8_t data[10];
    int dataSize = f.read(data, sizeof(data));
    XpUiReaderProgress progress{};
    const uint8_t loaded = xp_ui_reader_progress_decode(data, static_cast<uint32_t>(dataSize), &progress);
    if (loaded != 0) {
      currentSpineIndex = progress.spine;
      nextPageNumber = progress.page;
      if (loaded == 2) LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
      cachedSpineIndex = currentSpineIndex;
      LOG_DBG("ERS", "Loaded cache: %d, %d", currentSpineIndex, nextPageNumber);
      if (progress.has_count) cachedChapterTotalPageCount = progress.count;
      if (progress.has_offset) cachedVisibleTextOffset = progress.offset;
    }
  }

  if (currentSpineIndex == 0) {
    int textSpineIndex = epub->getSpineIndexForTextReference();
    if (textSpineIndex != 0) {
      currentSpineIndex = textSpineIndex;
      cachedVisibleTextOffset.reset();
      LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", textSpineIndex);
    }
  }

  loadLinkStack();
  loadCachedBookmarks();
  if (!CLIPPINGS.loadForBook(epub->getPath(), epub->getTitle(), epub->getAuthor(), "epub")) {
    LOG_ERR("CLIP", "Failed to load clippings for %s", epub->getPath().c_str());
  }
  return true;
}

ChapterPosition EpubReaderActivity::chapterPosition() const {
  if (section) return {section->currentPage, section->estimatedTotalPages()};
  return {nextPageNumber, cachedChapterTotalPageCount};
}

int EpubReaderActivity::bookPercentFor(const ChapterPosition& position) const {
  if (!epub || epub->getBookSize() == 0 || !position.hasTotal()) return 0;
  // The page index can run past the chapter's estimated total while it is still
  // building, so the fraction is clamped before the cast.
  const float fraction = epub->calculateProgress(currentSpineIndex, position.chapterFraction());
  return static_cast<int>(std::clamp(fraction, 0.0f, 1.0f) * 100.0f + 0.5f);
}

void EpubReaderActivity::openReaderMenu() {
  pendingManualTurn = 0;
  if (usesToolbarMenu()) {
    // Reached from a child activity's result handler (footnotes, bookmarks,
    // go-to-percent... cancelled back to the menu), so the framebuffer holds
    // that screen, not the page: re-render the page and let renderBook() put
    // the toolbar on top. The in-reader fast path is openOverlay().
    overlay = Overlay::Toolbar;
    focusedTool = 0;
    panelHoldJumped = false;
    panelCursorShown = !mappedInput.hasTouch();
    if (!toolbarUi) toolbarUi = std::make_unique<ReaderToolbarUi>(renderer);
    toolbarUi->begin();
    discardOverlayPage();
    requestUpdate();
    return;
  }
  // Child screens (chapter list, text settings) release the section to free its
  // pagination buffers; chapterPosition() covers that with the cached position.
  const ChapterPosition position = chapterPosition();
  const int bookProgressPercent = bookPercentFor(position);

  bool hasFootnotes;
  {
    RenderLock lock;
    hasFootnotes = !currentPageMetadata.footnotes().empty();
  }
  startActivityForResult(
      std::make_unique<EpubReaderMenuActivity>(renderer, mappedInput, epub->getTitle(), position.displayPage(),
                                               position.totalPages, bookProgressPercent, SETTINGS.orientation,
                                               hasFootnotes, !cachedBookmarks.empty(), CLIPPINGS.hasClippings()),
      [this](const ActivityResult& result) {
        const auto& menu = std::get<MenuResult>(result.data);

        if (SETTINGS.orientation != menu.orientation) {
          applyOrientation(menu.orientation);
        }

        toggleAutoPageTurn(menu.pageTurnOption);
        if (!result.isCancelled) {
          onReaderMenuConfirm(static_cast<EpubReaderMenuActivity::MenuAction>(menu.action));
        }
      });
}

// Runs on the render task, under its lock, right before every chapter build. The page turner's
// radio holds about 50 KB while it runs, and no heap figure taken before a build tells whether the
// build fits next to it (X3: with the radio up, 55 KB free and a 47 KB block, a 6 KB chapter ran
// out of memory). So the radio stops for every build and starts again once the chapter is built
// and its page is shown. A radio that did not stop within the module's 3 s, or a start still
// settling, usually lets go soon after: the build waits up to BUILD_ROOM_WAIT_MS more, then is
// refused rather than run next to it. The stop finishes on the main loop (the module's tick) and
// the start on its own task; neither needs the render lock this task holds.
bool EpubReaderActivity::makeRoomForChapterBuild() {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  stripBuffers.releaseIdle();
#endif
  bool asked = false;
  const auto roomMade = [this, &asked] {
    // The first ask stops the radio. A later one asks again only once a start that was still
    // settling has left the radio up.
    if (!radioReleasedForBuild.load() && (!asked || !bleturner::status().starting)) {
      radioReleasedForBuild = bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
    }
    asked = true;
    return chapterBuildMayRun(bleturner::status());
  };
  const auto nowMs = [] { return static_cast<uint32_t>(millis()); };
  const auto nap = [](const uint32_t ms) { delay(ms); };
  if (waitForBuildRoom(roomMade, nowMs, nap, BUILD_ROOM_WAIT_MS, BUILD_ROOM_STEP_MS)) return true;
  LOG_ERR("ERS", "Bluetooth radio still holds the heap after %u ms; chapter build refused",
          static_cast<unsigned>(BUILD_ROOM_WAIT_MS));
  return false;
}

bool EpubReaderActivity::buildTickHeapGate() {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t maxBlock = ESP.getMaxAllocHeap();
  buildHeapPaused = freeHeap < BACKGROUND_BUILD_MIN_FREE_HEAP || maxBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  if (buildHeapPaused && stripBuffers.releaseIdle()) {
    const size_t reclaimedFree = ESP.getFreeHeap();
    const size_t reclaimedBlock = ESP.getMaxAllocHeap();
    buildHeapPaused = reclaimedFree < BACKGROUND_BUILD_MIN_FREE_HEAP || reclaimedBlock < BACKGROUND_BUILD_MIN_MAX_ALLOC;
    LOG_DBG("ERS", "Released idle reader strips for build: heap %u/%u -> %u/%u", static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(maxBlock), static_cast<unsigned>(reclaimedFree),
            static_cast<unsigned>(reclaimedBlock));
  }
#endif
  return !buildHeapPaused;
}

void EpubReaderActivity::showBuildPopup() {
  if (!buildPopupPending || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, tr(STR_INDEXING));
  renderRefreshCounter() = 1;
  buildPopupPending = false;
}

// Debounced state of every button. Covers the frame between a raw release and its debounced
// release edge, which would otherwise run a build step before the page turn.
bool EpubReaderActivity::anyButtonPressed() const {
  using Button = MappedInputManager::Button;
  for (const Button button :
       {Button::Back, Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down, Button::Power}) {
    if (mappedInput.isPressed(button)) return true;
  }
  return false;
}

void EpubReaderActivity::backgroundBuildTick() {
  RenderLock lock(RenderLock::Mode::Try);
  // Not under an open toolbar or panel: those repaint straight onto the page in the framebuffer.
  if (!lock.ownsLock() || overlay != Overlay::None || !backgroundBuildWanted()) return;
  // A button is down or a page turn is queued: the next loop frames act on it first.
  if (buttonContactClosed() || anyButtonPressed() || pendingManualTurn != 0 || !buildTickHeapGate()) return;
  // A build step can lend the framebuffer (image probes), which hands it back white while the
  // panel still shows the page; redraw so nothing is later painted over the blank buffer.
  const uint32_t loansBefore = renderer.frameBufferLoanCount();
  const bool built = section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK, &buttonContactClosed);
  if (renderer.frameBufferLoanCount() != loansBefore) {
    pageBufferStale = true;
    requestUpdate();
  }
  if (!built) {
    LOG_ERR("ERS", "Background section build failed");
    backgroundBuildFailedSpine = currentSpineIndex;
    nextPageNumber = section->currentPage;
    section.reset();
    requestUpdate();
  } else if (section->isBuildComplete() && applyDeferredReposition()) {
    requestUpdate();
  }
}

void EpubReaderActivity::openDictionaryWordSelect() {
  if (SETTINGS.dictionaryName[0] == '\0') {
    showDictionaryMessage = true;
    dictionaryMessageTime = millis();
    requestUpdate();
    return;
  }
  startClipSelection(-1, -1, ClipSelectionActivity::Mode::Lookup);
}

void EpubReaderActivity::startClipSelection(const int initialX, const int initialY,
                                            const ClipSelectionActivity::Mode mode) {
  if (!section || !epub || section->currentPage < 0 || section->currentPage >= section->pageCount) return;

#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  {
    RenderLock lock;
    stripBuffers.releaseIdle();
  }
#endif
  const int pageNumber = section->currentPage;
  const uint16_t pageCount = section->pageCount;
  const uint16_t spineIndex = static_cast<uint16_t>(currentSpineIndex);
  std::vector<std::unique_ptr<Page>> pages;
  pages.reserve(CLIP_SELECTION_PAGE_WINDOW);
  for (int pageOffset = 0; pageOffset < CLIP_SELECTION_PAGE_WINDOW && pageNumber + pageOffset < pageCount;
       ++pageOffset) {
    auto page = section->loadPage(pageNumber + pageOffset);
    if (!page) break;
    if (pageOffset == 0 && initialX >= 0 && CLIPPINGS.hasClippings()) {
      RenderLock lock;
      const int clippingIndex = clippingAtPoint(*page, initialX, initialY);
      if (clippingIndex >= 0) {
        const bool removed = CLIPPINGS.removeClippingAt(static_cast<size_t>(clippingIndex));
        if (!removed) LOG_ERR("CLIP", "Failed to delete highlighted clipping %d", clippingIndex);
        clippingMessage = removed ? StrId::STR_CLIPPING_REMOVED : StrId::STR_CLIPPING_DELETE_FAILED;
        showClippingMessage = true;
        clippingMessageTime = millis();
        requestUpdate();
        return;
      }
    }
    pages.push_back(std::move(page));
  }
  if (pages.empty()) {
    LOG_ERR("CLIP", "Failed to load page %d for clipping selection", pageNumber);
    requestUpdate();
    return;
  }

  if (buildViewportWidth == 0 || buildViewportHeight == 0) {
    LOG_ERR("CLIP", "Cannot anchor clipping before the reader viewport is initialized");
    requestUpdate();
    return;
  }
  const uint32_t layoutSignature =
      readerRenderSpecSignature(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight));

  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;

  std::string chapterTitle;
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex >= 0) chapterTitle = epub->getTocItem(tocIndex).title;
  std::string bookTitle = epub->getTitle();
  std::string author = epub->getAuthor();

  auto activity = makeUniqueNoThrow<ClipSelectionActivity>(renderer, mappedInput, std::move(pages), marginLeft,
                                                           marginTop, initialX, initialY, mode);
  if (!activity) {
    LOG_ERR("CLIP", "Failed to allocate clipping selection activity");
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(activity), [this, spineIndex, pageNumber, pageCount, layoutSignature,
                                               bookTitle = std::move(bookTitle), author = std::move(author),
                                               chapterTitle = std::move(chapterTitle)](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto& clipping = std::get<ClippingResult>(result.data);
    const uint16_t startPage = static_cast<uint16_t>(pageNumber + clipping.startPageOffset);
    const uint16_t endPage = static_cast<uint16_t>(pageNumber + clipping.endPageOffset);
    if (section && currentSpineIndex == spineIndex && endPage < section->pageCount) {
      section->currentPage = endPage;
      currentPageVisibleOffset = section->getVisibleTextOffsetForPage(endPage);
    }
    if (clipping.action == ClippingResult::Action::Bookmark) {
      addBookmark();
      showBookmarkMessage = true;
      bookmarkMessageTime = millis();
      requestUpdate();
      return;
    }
    if (clipping.action == ClippingResult::Action::Lookup) {
      auto page = section ? section->loadPage(section->currentPage) : nullptr;
      if (!page) {
        LOG_ERR("CLIP", "Failed to load lookup page");
        requestUpdate();
        return;
      }
      int top, right, bottom, left;
      renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
      auto lookup = makeUniqueNoThrow<DictionaryWordSelectActivity>(renderer, mappedInput, std::move(page),
                                                                    left + SETTINGS.screenMargin,
                                                                    top + SETTINGS.screenMargin, clipping.text);
      if (!lookup) {
        LOG_ERR("CLIP", "OOM: dictionary lookup activity");
        requestUpdate();
        return;
      }
      startActivityForResult(std::move(lookup), [this](const ActivityResult& result) {
        if (!result.isCancelled) openDictionaryWordSelect();
      });
      return;
    }
    const uint16_t paragraphIndex =
        section ? section->getParagraphIndexForPage(startPage).value_or(UINT16_MAX) : UINT16_MAX;
    const auto addResult = CLIPPINGS.addClipping(
        spineIndex, startPage, endPage, pageCount, clipping.startWordIndex, clipping.endWordIndex, clipping.wordCount,
        chapterTitle.c_str(), paragraphIndex, clipping.text, layoutSignature, clipping.startOffset, clipping.endOffset);
    bool exported = false;
    if (addResult == ClippingStore::AddResult::Added) {
      exported = ClippingsManager::saveClipping(bookTitle, author, chapterTitle, startPage + 1, clipping.text);
    }
    clippingMessage = addResult == ClippingStore::AddResult::LimitReached ? StrId::STR_CLIPPING_LIMIT_REACHED
                      : exported                                          ? StrId::STR_CLIPPING_SAVED
                      : addResult == ClippingStore::AddResult::Added      ? StrId::STR_CLIPPING_EXPORT_FAILED
                                                                          : StrId::STR_CLIPPING_FAILED;
    showClippingMessage = true;
    clippingMessageTime = millis();
    requestUpdate();
  });
}

void EpubReaderActivity::openFootnoteSelect(const bool reopenMenuOnCancel) {
  std::string href;
  bool singleFootnote;
  std::unique_ptr<Page> page;
  {
    RenderLock lock;
    if (!section || currentPageMetadata.footnotes().empty()) return;
    singleFootnote = currentPageMetadata.footnotes().size() == 1;
    if (singleFootnote) {
      href = currentPageMetadata.footnotes()[0].href;
    } else {
      page = section->loadPage(section->currentPage);
    }
  }
  // Navigation acquires RenderLock itself; only owned data may leave the scope above.
  if (singleFootnote) {
    navigateToHref(href, true);
    return;
  }
  if (!page) return;

  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += SETTINGS.screenMargin;
  orientedMarginLeft += SETTINGS.screenMargin;
  auto selector = makeUniqueNoThrow<EpubReaderFootnoteSelectActivity>(renderer, mappedInput, std::move(page),
                                                                      orientedMarginLeft, orientedMarginTop);
  if (!selector) {
    LOG_ERR("ERA", "OOM: EpubReaderFootnoteSelectActivity");
    return;
  }
  startActivityForResult(std::move(selector), [this, reopenMenuOnCancel](const ActivityResult& result) {
    if (result.isCancelled) {
      if (reopenMenuOnCancel) {
        openReaderMenu();
      } else {
        requestUpdate();
      }
      return;
    }
    const auto& footnoteResult = std::get<FootnoteResult>(result.data);
    navigateToHref(footnoteResult.href, true);
    requestUpdate();
  });
}

void EpubReaderActivity::openClippings() {
  if (!CLIPPINGS.hasClippings()) {
    requestUpdate();
    return;
  }
  auto list = makeUniqueNoThrow<EpubReaderClippingListActivity>(renderer, mappedInput);
  if (!list) {
    LOG_ERR("CLIP", "OOM: clipping list");
    return;
  }
  startActivityForResult(std::move(list), [this](const ActivityResult& result) {
    if (!result.isCancelled) {
      if (const auto* progress = std::get_if<ProgressChangeResult>(&result.data)) {
        applyProgressChange(*progress);
        return;
      }
    }
    requestUpdate();
  });
}

static_assert(static_cast<uint8_t>(HomeButtonAction::Ignore) == 1);

bool EpubReaderActivity::tryIdleLoop() {
  // This sampler is valid only while rendering excludes resource work.
  if (!RenderLock::peek()) return false;
  XpUiReaderIdleSnapshot sample{};
  sample.busy = 1;
  sample.has_epub = bool(epub);
  sample.spine_count = epub ? epub->getSpineItemsCount() : 0;
  sample.load_popup = loadFailurePopup.isActive();
  sample.end_options = bool(endOfBookOptions);
  sample.touch = mappedInput.hasTouch();
  sample.remote = mappedInput.hasRemoteClick();
  sample.home_action = static_cast<uint8_t>(mappedInput.homeButtonAction());
  sample.edges = gpio.wasAnyPressed() || gpio.wasAnyReleased();
  for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button) {
    if (gpio.isPressed(button)) sample.held_buttons |= 1u << button;
  }
  sample.orientation = SETTINGS.orientation;
  sample.long_button = SETTINGS.longPressButtonBehavior;
  sample.short_power = SETTINGS.shortPwrBtn;
  sample.tilt = SETTINGS.tiltPageTurn;
  sample.confirm_threshold = confirmLongPressThreshold();
  sample.touch_controls = SETTINGS.touchReaderControls;
  {
    ModelLock lock(*this);
    sample.remembered = baseModel.book_remembered;
    sample.spine = currentSpineIndex;
    sample.overlay = static_cast<uint8_t>(overlay);
    sample.automatic = automaticPageTurnActive;
    sample.pending_turn = pendingManualTurn;
    sample.bookmark_notice = showBookmarkMessage;
    sample.dictionary_notice = showDictionaryMessage;
    sample.font_notice = showFontDamagedMessage;
    sample.clipping_notice = showClippingMessage;
    sample.read_move = pendingReadFolderMove;
    sample.recents_removed = recentsEntryRemoved;
    sample.applied_orientation = appliedOrientation;
    sample.footnote_depth = static_cast<uint8_t>(footnoteDepth);
  }
  const uint8_t plan = xp_ui_reader_idle_plan(&sample);
  if (!(plan & XP_UI_READER_IDLE_ADMITTED)) return false;
  // Button-only frames still perform every potentially consuming mapped probe.
  if (plan & XP_UI_READER_IDLE_SCREEN_LONG) {
    int x = 0, y = 0;
    mappedInput.wasScreenLongPress(x, y);
  }
  mappedInput.wasBackGesture();
  mappedInput.wasLongPressed(MappedInputManager::Button::Back, ReaderUtils::GO_BACK_OR_HOME_MS);
  mappedInput.wasReleased(MappedInputManager::Button::Back);
  return true;
}

void EpubReaderActivity::loop() {
  if (tryIdleLoop()) return;
  static_assert(static_cast<uint32_t>(Overlay::None) == XP_UI_READER_VALUE_OVERLAY_NONE);
  static_assert(static_cast<uint32_t>(Overlay::Toolbar) == XP_UI_READER_VALUE_OVERLAY_TOOLBAR);
  using Cache = std::remove_pointer_t<decltype(renderer.getFontCacheManager())>;
  struct Context {
    EpubReaderActivity& o;
    std::optional<RenderLock> lock{};
    std::unique_ptr<Page> page{};
    Cache* cache = nullptr;
    std::optional<decltype(std::declval<Cache&>().createPrewarmScope())> prewarm{};
    std::string href{};
    uint32_t readModel(uint8_t field) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderLoopField>(field)) {
        case XP_UI_READER_LOOP_FIELD_RADIO_RELEASED:
          return static_cast<uint32_t>(o.radioReleasedForBuild.load());
        case XP_UI_READER_LOOP_FIELD_APPLIED_ORIENTATION:
          return static_cast<uint32_t>(o.appliedOrientation);
        case XP_UI_READER_LOOP_FIELD_LAST_RENDERED:
          return static_cast<uint32_t>(o.lastRenderCompleteMs);
        case XP_UI_READER_LOOP_FIELD_IDLE_SPINE:
          return static_cast<uint32_t>(o.idlePrewarmSpine);
        case XP_UI_READER_LOOP_FIELD_IDLE_PAGE:
          return static_cast<uint32_t>(o.idlePrewarmPage);
        case XP_UI_READER_LOOP_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_LOOP_FIELD_BUILD_WIDTH:
          return static_cast<uint32_t>(o.buildViewportWidth);
        case XP_UI_READER_LOOP_FIELD_BUILD_HEIGHT:
          return static_cast<uint32_t>(o.buildViewportHeight);
        case XP_UI_READER_LOOP_FIELD_PARTIAL_FAILED:
          return static_cast<uint32_t>(o.partialRebuildStartFailed);
        case XP_UI_READER_LOOP_FIELD_RECENTS_REMOVED:
          return static_cast<uint32_t>(o.recentsEntryRemoved);
        case XP_UI_READER_LOOP_FIELD_PENDING_READ_MOVE:
          return static_cast<uint32_t>(o.pendingReadFolderMove);
        case XP_UI_READER_LOOP_FIELD_SHOW_BOOKMARK:
          return static_cast<uint32_t>(o.showBookmarkMessage);
        case XP_UI_READER_LOOP_FIELD_BOOKMARK_TIME:
          return static_cast<uint32_t>(o.bookmarkMessageTime);
        case XP_UI_READER_LOOP_FIELD_SHOW_DICTIONARY:
          return static_cast<uint32_t>(o.showDictionaryMessage);
        case XP_UI_READER_LOOP_FIELD_DICTIONARY_TIME:
          return static_cast<uint32_t>(o.dictionaryMessageTime);
        case XP_UI_READER_LOOP_FIELD_SHOW_FONT:
          return static_cast<uint32_t>(o.showFontDamagedMessage);
        case XP_UI_READER_LOOP_FIELD_FONT_TIME:
          return static_cast<uint32_t>(o.fontDamagedMessageTime);
        case XP_UI_READER_LOOP_FIELD_OVERLAY:
          return static_cast<uint32_t>(o.overlay);
        case XP_UI_READER_LOOP_FIELD_LAST_TURN:
          return static_cast<uint32_t>(o.lastPageTurnTime);
        case XP_UI_READER_LOOP_FIELD_AUTO_ACTIVE:
          return static_cast<uint32_t>(o.automaticPageTurnActive);
        case XP_UI_READER_LOOP_FIELD_PENDING_TURN:
          return static_cast<uint32_t>(o.pendingManualTurn);
        case XP_UI_READER_LOOP_FIELD_PENDING_TOUCH:
          return static_cast<uint32_t>(o.pendingManualTurnTouch);
        case XP_UI_READER_LOOP_FIELD_TURN_DURATION:
          return static_cast<uint32_t>(o.pageTurnDuration);
        case XP_UI_READER_LOOP_FIELD_SHOW_CLIP:
          return static_cast<uint32_t>(o.showClippingMessage);
        case XP_UI_READER_LOOP_FIELD_CLIP_TIME:
          return static_cast<uint32_t>(o.clippingMessageTime);
        case XP_UI_READER_LOOP_FIELD_FOOTNOTE_DEPTH:
          return static_cast<uint32_t>(o.footnoteDepth);
      }
      return 0;
    }
    void writeModel(uint8_t field, uint32_t value) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderLoopField>(field)) {
        case XP_UI_READER_LOOP_FIELD_RADIO_RELEASED:
          o.radioReleasedForBuild = value != 0;
          break;
        case XP_UI_READER_LOOP_FIELD_APPLIED_ORIENTATION:
          o.appliedOrientation = static_cast<decltype(o.appliedOrientation)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_LAST_RENDERED:
          o.lastRenderCompleteMs = static_cast<decltype(o.lastRenderCompleteMs)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_IDLE_SPINE:
          o.idlePrewarmSpine = static_cast<decltype(o.idlePrewarmSpine)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_IDLE_PAGE:
          o.idlePrewarmPage = static_cast<decltype(o.idlePrewarmPage)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_SPINE:
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_BUILD_WIDTH:
          o.buildViewportWidth = static_cast<decltype(o.buildViewportWidth)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_BUILD_HEIGHT:
          o.buildViewportHeight = static_cast<decltype(o.buildViewportHeight)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_PARTIAL_FAILED:
          o.partialRebuildStartFailed = static_cast<decltype(o.partialRebuildStartFailed)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_RECENTS_REMOVED:
          o.recentsEntryRemoved = static_cast<decltype(o.recentsEntryRemoved)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_PENDING_READ_MOVE:
          o.pendingReadFolderMove = static_cast<decltype(o.pendingReadFolderMove)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_SHOW_BOOKMARK:
          o.showBookmarkMessage = static_cast<decltype(o.showBookmarkMessage)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_BOOKMARK_TIME:
          o.bookmarkMessageTime = static_cast<decltype(o.bookmarkMessageTime)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_SHOW_DICTIONARY:
          o.showDictionaryMessage = static_cast<decltype(o.showDictionaryMessage)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_DICTIONARY_TIME:
          o.dictionaryMessageTime = static_cast<decltype(o.dictionaryMessageTime)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_SHOW_FONT:
          o.showFontDamagedMessage = static_cast<decltype(o.showFontDamagedMessage)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_FONT_TIME:
          o.fontDamagedMessageTime = static_cast<decltype(o.fontDamagedMessageTime)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_OVERLAY:
          o.overlay = static_cast<decltype(o.overlay)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_LAST_TURN:
          o.lastPageTurnTime = static_cast<decltype(o.lastPageTurnTime)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_AUTO_ACTIVE:
          o.automaticPageTurnActive = static_cast<decltype(o.automaticPageTurnActive)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_PENDING_TURN:
          o.pendingManualTurn = static_cast<decltype(o.pendingManualTurn)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_PENDING_TOUCH:
          o.pendingManualTurnTouch = static_cast<decltype(o.pendingManualTurnTouch)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_TURN_DURATION:
          o.pageTurnDuration = static_cast<decltype(o.pageTurnDuration)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_SHOW_CLIP:
          o.showClippingMessage = static_cast<decltype(o.showClippingMessage)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_CLIP_TIME:
          o.clippingMessageTime = static_cast<decltype(o.clippingMessageTime)>(value);
          break;
        case XP_UI_READER_LOOP_FIELD_FOOTNOTE_DEPTH:
          o.footnoteDepth = static_cast<decltype(o.footnoteDepth)>(value);
          break;
      }
    }
    uint32_t query(uint8_t query) {
      switch (static_cast<XpUiReaderLoopQuery>(query)) {
        case XP_UI_READER_LOOP_QUERY_LOAD_POPUP:
          return o.loadFailurePopup.isActive();
        case XP_UI_READER_LOOP_QUERY_HAS_EPUB:
          return bool(o.epub);
        case XP_UI_READER_LOOP_QUERY_HAS_SECTION:
          return bool(o.section);
        case XP_UI_READER_LOOP_QUERY_BUILDING:
          return o.section->isBuilding();
        case XP_UI_READER_LOOP_QUERY_FRAMEBUFFER:
          return o.renderer.hasFrameBuffer();
        case XP_UI_READER_LOOP_QUERY_NOW:
          return millis();
        case XP_UI_READER_LOOP_QUERY_FREE_HEAP:
          return ESP.getFreeHeap();
        case XP_UI_READER_LOOP_QUERY_MAX_BLOCK:
          return ESP.getMaxAllocHeap();
        case XP_UI_READER_LOOP_QUERY_PAGE:
          return o.section->currentPage;
        case XP_UI_READER_LOOP_QUERY_PAGE_COUNT:
          return o.section->pageCount;
        case XP_UI_READER_LOOP_QUERY_PARTIAL:
          return o.section->isPartial();
        case XP_UI_READER_LOOP_QUERY_RADIO_HEAP:
          return bleturner::holdsHeap();
        case XP_UI_READER_LOOP_QUERY_SPINE_COUNT:
          return o.epub->getSpineItemsCount();
        case XP_UI_READER_LOOP_QUERY_REMOVE_RECENTS:
          return SETTINGS.removeReadBooksFromRecents;
        case XP_UI_READER_LOOP_QUERY_MOVE_FINISHED:
          return SETTINGS.moveFinishedToReadFolder;
        case XP_UI_READER_LOOP_QUERY_IN_READ_FOLDER:
          return isInReadFolder(o.epub->getPath());
        case XP_UI_READER_LOOP_QUERY_ORIENTATION:
          return SETTINGS.orientation;
        case XP_UI_READER_LOOP_QUERY_TOOLBAR_MENU:
          return o.usesToolbarMenu();
        case XP_UI_READER_LOOP_QUERY_TOUCH_CONTROLS:
          return SETTINGS.touchReaderControls;
        case XP_UI_READER_LOOP_QUERY_HOME_ACTION:
          return static_cast<uint32_t>(o.mappedInput.homeButtonAction());
        case XP_UI_READER_LOOP_QUERY_TOUCH_MENU:
          return ReaderUtils::isTouchMenuGesture(o.renderer, o.mappedInput);
        case XP_UI_READER_LOOP_QUERY_CONFIRM_RELEASE:
          return o.mappedInput.wasReleased(MappedInputManager::Button::Confirm);
        case XP_UI_READER_LOOP_QUERY_BACK_RELEASE:
          return o.mappedInput.wasReleased(MappedInputManager::Button::Back);
        case XP_UI_READER_LOOP_QUERY_BUSY:
          return RenderLock::peek();
        case XP_UI_READER_LOOP_QUERY_END_MENU_ACTIVE:
          return o.endOfBookMenuActive();
        case XP_UI_READER_LOOP_QUERY_CONFIRM_THRESHOLD:
          return o.confirmLongPressThreshold();
        case XP_UI_READER_LOOP_QUERY_LONG_MENU_FUNCTION:
          return SETTINGS.longPressMenuFunction;
        case XP_UI_READER_LOOP_QUERY_HAS_TOUCH:
          return o.mappedInput.hasTouch();
        case XP_UI_READER_LOOP_QUERY_HELD:
          return o.mappedInput.getHeldTime();
        case XP_UI_READER_LOOP_QUERY_SHORT_POWER:
          return SETTINGS.shortPwrBtn;
        case XP_UI_READER_LOOP_QUERY_POWER_RELEASE:
          return o.mappedInput.wasReleased(MappedInputManager::Button::Power);
        case XP_UI_READER_LOOP_QUERY_DOWN_RELEASE:
          return o.mappedInput.wasReleased(MappedInputManager::Button::Down);
        case XP_UI_READER_LOOP_QUERY_LONG_BUTTON_BEHAVIOR:
          return SETTINGS.longPressButtonBehavior;
      }
      return 0;
    }
    uint32_t command(uint8_t cmd, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderLoopCommand>(cmd)) {
        case XP_UI_READER_LOOP_COMMAND_POPUP_INPUT:
          o.loadFailurePopup.handleInput(o.mappedInput, [owner = &o] { owner->requestUpdate(); });
          break;
        case XP_UI_READER_LOOP_COMMAND_FINISH:
          o.finish();
          break;
        case XP_UI_READER_LOOP_COMMAND_REMEMBER:
          o.rememberBookOnceRendered();
          break;
        case XP_UI_READER_LOOP_COMMAND_TRY_LOCK:
          assert(!lock);
          lock.emplace(RenderLock::Mode::Try);
          return lock->ownsLock();
          break;
        case XP_UI_READER_LOOP_COMMAND_UNLOCK:
          assert(lock);
          lock.reset();
          break;
        case XP_UI_READER_LOOP_COMMAND_RADIO_AFTER:
          bleturner::afterPaint();
          break;
        case XP_UI_READER_LOOP_COMMAND_REPAINT_LINK_NOTE:
          return o.linkNoteTitle.repaint(bleturner::linkNote());
          break;
        case XP_UI_READER_LOOP_COMMAND_UPDATE:
          o.requestUpdate();
          break;
        case XP_UI_READER_LOOP_COMMAND_ORIENTATION:
          o.applyOrientation(a);
          break;
        case XP_UI_READER_LOOP_COMMAND_LOAD_PAGE:
          page = o.section->loadPage(a);
          return bool(page);
          break;
        case XP_UI_READER_LOOP_COMMAND_FONT_CACHE:
          cache = o.renderer.getFontCacheManager();
          return cache != nullptr;
          break;
        case XP_UI_READER_LOOP_COMMAND_PREWARM_BEGIN:
          prewarm.emplace(cache->createPrewarmScope());
          break;
        case XP_UI_READER_LOOP_COMMAND_PREWARM_PAGE:
          page->render(o.renderer, SETTINGS.getReaderFontId(), 0, 0);
          break;
        case XP_UI_READER_LOOP_COMMAND_PREWARM_SCAN_END:
          prewarm->endScanAndPrewarm();
          break;
        case XP_UI_READER_LOOP_COMMAND_PREWARM_LOG:
          LOG_DBG("ERS", "Idle prewarm: page %d in %lums", static_cast<int>(a),
                  millis() - static_cast<unsigned long>(b));
          break;
        case XP_UI_READER_LOOP_COMMAND_PREWARM_DROP:
          prewarm.reset();
          break;
        case XP_UI_READER_LOOP_COMMAND_PAGE_DROP:
          page.reset();
          break;
        case XP_UI_READER_LOOP_COMMAND_START_BUILD:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          o.stripBuffers.releaseIdle();
#endif
          return o.section->startBuild(SETTINGS.readerRenderSpec(o.buildViewportWidth, o.buildViewportHeight));
          break;
        case XP_UI_READER_LOOP_COMMAND_BUILD_FAILED_LOG:
          LOG_ERR("ERS", "Failed to start deferred partial extension build");
          break;
        case XP_UI_READER_LOOP_COMMAND_BUILD_STARTED_LOG:
          LOG_DBG("ERS", "Reader near partial watermark (%d/%d), resuming extension build", o.section->currentPage,
                  o.section->pageCount);
          break;
        case XP_UI_READER_LOOP_COMMAND_CLEAR_END:
          o.clearEndOfBookOptionsIfNeeded();
          break;
        case XP_UI_READER_LOOP_COMMAND_REMOVE_RECENT:
          return RECENT_BOOKS.removeByPath(o.epub->getPath());
          break;
        case XP_UI_READER_LOOP_COMMAND_ADD_RECENT:
          RECENT_BOOKS.addBook(o.epub->getPath(), o.epub->getTitle(), o.epub->getAuthor(), o.epub->getThumbBmpPath());
          break;
        case XP_UI_READER_LOOP_COMMAND_OVERLAY_INPUT:
          o.handleOverlayInput();
          break;
        case XP_UI_READER_LOOP_COMMAND_DISCARD_OVERLAY:
          o.discardOverlayPage();
          break;
        case XP_UI_READER_LOOP_COMMAND_CLIP:
          o.startClipSelection(a, b);
          break;
        case XP_UI_READER_LOOP_COMMAND_TURN:
          return o.pageTurn(a != 0);
          break;
        case XP_UI_READER_LOOP_COMMAND_NOTE_TURN:
          o.notePageTurn(a != 0, b != 0);
          break;
        case XP_UI_READER_LOOP_COMMAND_HAPTIC:
          haptic_feedback::touchAction(a != 0);
          break;
        case XP_UI_READER_LOOP_COMMAND_END_MENU:
          return o.handleEndOfBookMenu();
          break;
        case XP_UI_READER_LOOP_COMMAND_CONFIRM_LONG:
          return o.mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, a);
          break;
        case XP_UI_READER_LOOP_COMMAND_BOOKMARK:
          o.addBookmark();
          break;
        case XP_UI_READER_LOOP_COMMAND_SYNC:
          return o.launchKOReaderSync();
          break;
        case XP_UI_READER_LOOP_COMMAND_DICTIONARY:
          o.openDictionaryWordSelect();
          break;
        case XP_UI_READER_LOOP_COMMAND_RESTORE_POSITION:
          o.restoreSavedPosition();
          break;
        case XP_UI_READER_LOOP_COMMAND_FOOTNOTES:
          o.openFootnoteSelect(false);
          break;
        case XP_UI_READER_LOOP_COMMAND_OPEN_OVERLAY:
          o.openOverlay(static_cast<Overlay>(a));
          break;
        case XP_UI_READER_LOOP_COMMAND_OPEN_MENU:
          o.openReaderMenu();
          break;
        case XP_UI_READER_LOOP_COMMAND_MATCH_LINK: {
          RenderLock held;
          const auto* link = EpubReaderUtils::linkAtPoint(o.currentPageMetadata.links(), a, b,
                                                          o.currentPageLinkMarginLeft, o.currentPageLinkMarginTop);
          if (link) href = link->href;
          return link != nullptr;
        } break;
        case XP_UI_READER_LOOP_COMMAND_NAVIGATE_LINK:
          o.navigateToHref(href, true);
          break;
        case XP_UI_READER_LOOP_COMMAND_BACK_NAVIGATION:
          return o.handleBackNavigation();
          break;
        case XP_UI_READER_LOOP_COMMAND_BUILD_TICK:
          o.backgroundBuildTick();
          break;
        case XP_UI_READER_LOOP_COMMAND_END_TURN:
          return o.handleEndOfBookPageTurn(a != 0, b != 0);
          break;
        case XP_UI_READER_LOOP_COMMAND_SKIP:
          return o.skipPages(a);
          break;
      }
      return 0;
    }
  } context{*this};
  const XpUiReaderLoopServices services{
      &context,
      [](void* p, uint8_t f) { return static_cast<Context*>(p)->readModel(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->writeModel(f, v); },
      [](void* p, uint8_t q) { return static_cast<Context*>(p)->query(q); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); },
      [](void* p) -> XpUiReaderTurnInput {
        auto& o = static_cast<Context*>(p)->o;
        auto t = ReaderUtils::detectTouchPageTurn(o.renderer, o.mappedInput,
                                                  ReaderUtils::isRtlBookLanguage(o.epub->getLanguage()));
        return {static_cast<uint32_t>(t.heldMs), t.prev, t.next, 0, 0};
      },
      [](void* p) -> XpUiReaderTurnInput {
        auto& o = static_cast<Context*>(p)->o;
        auto [prev, next, tilt] = ReaderUtils::detectPageTurn(o.mappedInput);
        return {0, prev, next, tilt, 0};
      },
      [](void* p, uint8_t q, int32_t* x, int32_t* y) -> uint8_t {
        auto& o = static_cast<Context*>(p)->o;
        int a = 0, b = 0;
        bool ok = q == XP_UI_READER_LOOP_POINT_LONG ? o.mappedInput.wasScreenLongPress(a, b)
                                                    : o.mappedInput.wasScreenTapped(a, b);
        *x = a;
        *y = b;
        return ok;
      }};
  xp_ui_reader_loop_run(&services);
}

void EpubReaderActivity::jumpToPercent(int percent) {
  runNavigationPolicy(XP_UI_READER_NAV_OPERATION_PERCENT, percent);
}

void EpubReaderActivity::applyProgressChange(const ProgressChangeResult& sync) {
  runNavigationPolicy(XP_UI_READER_NAV_OPERATION_PROGRESS, 0, 0, 0, nullptr, &sync);
}

int EpubReaderActivity::currentTocIndex() const {
  if (!epub) return 0;
  if (section) return std::max(0, section->getTocIndexForPage(section->currentPage));
  // Child screens may have released the section; its saved anchor map remains readable.
  Section cachedSection(epub, currentSpineIndex, renderer);
  return std::max(0, cachedSection.getTocIndexForPage(nextPageNumber));
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  {
    RenderLock lock;
    stripBuffers.releaseIdle();
  }
#endif
  auto progressChangeResultHandler = [this](const ActivityResult& result) {
    loadCachedBookmarks();
    if (result.isCancelled) {
      openReaderMenu();
    } else {
      const auto& sync = std::get<ProgressChangeResult>(result.data);

      applyProgressChange(sync);
    }
  };

  switch (action) {
    case EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER: {
      int tocIndex;
      // Release the section while the chapter list is up (mirrors the
      // TEXT_SETTINGS path): picking a chapter resets it anyway, and its
      // tens-of-KB footprint is the difference between the chapter list
      // holding its CJK glyph arena (RAM-only repaints) and re-reading
      // glyphs from SD on every row step. Cancel restores via the same
      // cached-position rebuild TEXT_SETTINGS uses.
      {
        RenderLock lock;
        tocIndex = currentTocIndex();
        if (section) {
          rememberCurrentContentOffset();
          cachedSpineIndex = currentSpineIndex;
          cachedChapterTotalPageCount = section->pageCount;
          nextPageNumber = section->currentPage;
        }
        section.reset();
      }
      startActivityForResult(
          std::make_unique<EpubReaderChapterSelectionActivity>(renderer, mappedInput, epub, tocIndex),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
              return;
            }
            const auto& chapterResult = std::get<ChapterResult>(result.data);
            RenderLock lock;
            clearDeferredReposition();
            currentSpineIndex = chapterResult.spineIndex;
            pendingAnchor = chapterResult.anchor;
            nextPageNumber = 0;
            section.reset();
            requestUpdate();
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::FOOTNOTES: {
      openFootnoteSelect(true);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS: {
      startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                    TextSettingsActivity::Tab::Family),
                             [this](const ActivityResult&) {
                               {
                                 RenderLock lock;
                                 if (section) {
                                   rememberCurrentContentOffset();
                                   cachedSpineIndex = currentSpineIndex;
                                   cachedChapterTotalPageCount = section->pageCount;
                                   nextPageNumber = section->currentPage;
                                 }
                                 section.reset();
                               }
                               openReaderMenu();
                             });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BLUETOOTH:
    case EpubReaderMenuActivity::MenuAction::NIGHT_MODE:
      // Handled in-place by EpubReaderMenuActivity so its On/Off value updates
      // without closing the menu.
      break;
    case EpubReaderMenuActivity::MenuAction::FRONTLIGHT:
      // Handled in-place by EpubReaderMenuActivity using the live frontlight HAL.
      break;
    case EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT: {
      const int initialPercent = bookPercentFor(chapterPosition());
      startActivityForResult(
          std::make_unique<EpubReaderPercentSelectionActivity>(renderer, mappedInput, initialPercent),
          [this](const ActivityResult& result) {
            if (result.isCancelled) {
              openReaderMenu();
            } else {
              jumpToPercent(std::get<PercentResult>(result.data).percent);
            }
          });
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DICTIONARY: {
      openDictionaryWordSelect();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::DISPLAY_QR: {
      if (section && section->currentPage >= 0 && section->currentPage < section->pageCount) {
        std::string fullText = section->getTextFromSectionFile();
        if (!fullText.empty()) {
          startActivityForResult(std::make_unique<QrDisplayActivity>(renderer, mappedInput, fullText),
                                 [this](const ActivityResult&) { openReaderMenu(); });
          break;
        }
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::GO_HOME: {
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::DELETE_CACHE: {
      {
        RenderLock lock;
        if (epub && section) {
          uint16_t backupSpine = currentSpineIndex;
          uint16_t backupPage = section->currentPage;
          uint16_t backupPageCount = section->pageCount;
          section.reset();
          epub->clearCache();
          epub->setupCacheDir();
          if (!saveProgress(backupSpine, backupPage, backupPageCount)) {
            LOG_ERR("ERS", "Failed to save progress before cache clear");
          }
        }
      }
      onGoHome();
      return;
    }
    case EpubReaderMenuActivity::MenuAction::SCREENSHOT: {
      {
        RenderLock lock;
        pendingScreenshot = true;
      }
      requestUpdate();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SYNC: {
      launchKOReaderSync();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::BOOKMARKS: {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, epub, epub->getPath()),
          progressChangeResultHandler);
      break;
    }
    case EpubReaderMenuActivity::MenuAction::SAVE_CLIPPING: {
      startClipSelection();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::VIEW_CLIPPINGS: {
      openClippings();
      break;
    }
    case EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK: {
      addBookmark();
      break;
    }
  }
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  // The setting is hidden on these boards, so a stale stored value must not act.
  if (BoardConfig::hasTouch()) return 0;
  switch (SETTINGS.longPressMenuFunction) {
    case CrossPointSettings::LP_MENU_BOOKMARK:
    case CrossPointSettings::LP_MENU_DICTIONARY:
    case CrossPointSettings::LP_MENU_CREATE_CLIPPING:
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      return ReaderUtils::BOOKMARK_HOLD_MS;
    case CrossPointSettings::LP_MENU_KOSYNC:
      return KOREADER_STORE.hasCredentials() ? ReaderUtils::GO_HOME_MS : 0;
    case CrossPointSettings::LP_MENU_READER_MENU:
    case CrossPointSettings::LP_MENU_DISABLED:
    default:
      return 0;
  }
}

bool EpubReaderActivity::launchKOReaderSync() {
  flushReaderSession();

  // Without a KOReader server the sync activity only brings the device online
  // and drains that outbox; a failed join exits back to the reader with the
  // events still queued.
  if (!KOREADER_STORE.hasCredentials()) {
    activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
        renderer, mappedInput, epub->getPath(), getCurrentPosition(), SavedProgressPosition{}, "",
        /*pluginEventsOnly=*/true));
    return true;
  }

  RenderLock renderLock;

  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;

  CrossPointPosition localPos = getCurrentPosition();
  SavedProgressPosition localKoPos;
  const int tocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";
  const std::string savedEpubPath = epub->getPath();

  if (!saveProgress(currentSpineIndex, currentPage, totalPages)) {
    LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
    pendingSyncSaveError = true;
    requestUpdate();
    return true;
  }

  LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
  {
    if (section) {
      nextPageNumber = section->currentPage;
    }
    discardOverlayPage();
    ImageBlock::releaseRenderCache();
    ImageBlock::setExtractor(nullptr, nullptr);
    section.reset();
    if (auto* fcm = renderer.getFontCacheManager()) {
      fcm->releaseSdFontCaches();
    }
    // No rendering may run while the chapter mapper borrows the framebuffer.
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      localKoPos = ProgressMapper::toSavedProgress(epub, localPos);
    }
    // The destructor can no longer save the back-stack once epub is gone.
    if (footnoteDepth > 0) saveLinkStack();
    epub.reset();
  }
  LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());

  activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
      renderer, mappedInput, savedEpubPath, localPos, std::move(localKoPos), std::move(localChapterName)));
  return true;
}

void EpubReaderActivity::applyInitialOrientation() {
  ReaderActivity::applyInitialOrientation();
  appliedOrientation = SETTINGS.orientation;
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  // Also runs when SETTINGS already holds the new value but this layout was
  // built for the old one — that is what an external change looks like here.
  if (SETTINGS.orientation == orientation && appliedOrientation == orientation) {
    return;
  }

  RenderLock lock(*this);
  if (section) {
    rememberCurrentContentOffset();
    cachedSpineIndex = currentSpineIndex;
    cachedChapterTotalPageCount = section->pageCount;
    nextPageNumber = section->currentPage;
  }

  if (SETTINGS.orientation != orientation) {
    SETTINGS.orientation = orientation;
    SETTINGS.saveToFile();
  }
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  appliedOrientation = orientation;
  section.reset();
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  if (selectedPageTurnOption == 0 || selectedPageTurnOption >= std::size(PAGE_TURN_RATES)) {
    automaticPageTurnActive = false;
    return;
  }

  lastPageTurnTime = millis();
  pageTurnDuration = (1UL * 60 * 1000) / PAGE_TURN_RATES[selectedPageTurnOption];
  automaticPageTurnActive = true;

  const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    section.reset();
  }
}

bool EpubReaderActivity::pageTurn(const bool isForwardTurn) {
  if (!section) return false;
  bleturner::acknowledgeLinkNote();
  {
    RenderLock lock;
    clearDeferredReposition();
  }
  const auto move = xp_ui_reader_turn(currentSpineIndex, epub->getSpineItemsCount(), section->currentPage,
                                      section->pageCount, section->isBuilding(), isForwardTurn);
  return applyPageMove(move, true);
}

bool EpubReaderActivity::applyPageMove(const XpUiReaderMove& move, const bool recordTurn) {
  switch (static_cast<XpUiReaderMoveKind>(move.kind)) {
    case XP_UI_READER_MOVE_NONE:
      return false;
    case XP_UI_READER_MOVE_PAGE:
      section->currentPage = move.value;
      break;
    case XP_UI_READER_MOVE_CHAPTER: {
      RenderLock lock;
      nextPageNumber = 0;
      if (move.last_page) pendingPageJump = std::numeric_limits<uint16_t>::max();
      currentSpineIndex = move.value;
      section.reset();
      if (recordTurn) lastPageTurnTime = millis();
      return true;
    }
    case XP_UI_READER_MOVE_END:
      currentSpineIndex = move.value;
      break;
    default:
      return false;
  }
  if (recordTurn) lastPageTurnTime = millis();
  return true;
}

bool EpubReaderActivity::skipPages(const int amount) {
  if (!section) return false;
  bleturner::acknowledgeLinkNote();
  return applyPageMove(xp_ui_reader_skip(currentSpineIndex, section->currentPage, amount), false);
}

// Failed protected open: show the standard option dialog (wrapped message)
// instead of silently falling back to the previous screen. Exact error
// strings are set by openProtectedBook (ContentProtection.cpp).
bool EpubReaderActivity::handleLoadFailure() {
  if (loadProtectionError.empty()) return false;
  const std::string& perr = loadProtectionError;
  StrId msg = StrId::STR_DRM_PROTECTED_FILE;
  bool offerSync = false;
  if (perr == "access expired") {
    msg = StrId::STR_LOAN_EXPIRED;
  } else if (perr == "loan date unverified") {
    msg = StrId::STR_LOAN_TIME_UNVERIFIED;
    offerSync = true;
  }
  const char* options[2] = {I18N.get(offerSync ? StrId::STR_CLOCK_SYNC_NOW : StrId::STR_OK_BUTTON),
                            I18N.get(StrId::STR_OK_BUTTON)};
  loadFailurePopup.showMessage("", I18N.get(msg), options, offerSync ? 2 : 1, 0, [this, offerSync](const int index) {
    if (offerSync && index == 0) {
      beginLoanTimeSync();
      return;
    }
    finish();
  });
  requestUpdate();
  return true;  // stay alive; the popup's Back dismiss lands in loop()'s !epub finish
}

void EpubReaderActivity::beginLoanTimeSync() {
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("ERS", "OOM: Wi-Fi selection for loan time sync");
    finish();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || WiFi.status() != WL_CONNECTED) {
      finish();
      return;
    }
    GUI.drawPopup(renderer, tr(STR_SYNCING_TIME));
    const bool synced = trustedtime::syncNow(5000);
    WiFi.disconnect(false);
    delay(30);
    if (!synced) {
      // Reopening would hit the same unverified-time refusal: offer a retry.
      const char* options[2] = {I18N.get(StrId::STR_RETRY), I18N.get(StrId::STR_OK_BUTTON)};
      loadFailurePopup.showMessage("", I18N.get(StrId::STR_CLOCK_SYNC_FAIL), options, 2, 0, [this](const int index) {
        if (index == 0) {
          beginLoanTimeSync();
          return;
        }
        finish();
      });
      requestUpdate();
      return;
    }
    APP_STATE.openEpubPath = bookPath;
    APP_STATE.saveToFile();
    // Reboot straight back into this book with a clean heap (no-op on touch
    // boards, which fall through to the in-place relaunch below).
    silentRestartToReader();
    activityManager.goToReader(bookPath);
  });
}

void EpubReaderActivity::render(RenderLock&& lock) {
  if (loadFailurePopup.isActive()) {
    renderer.clearScreen();
    loadFailurePopup.processRender(renderer, mappedInput);
    return;
  }
  ReaderActivity::render(std::move(lock));
}

bool EpubReaderActivity::remoteShortcut(const bool readerMenu) {
  if (!section) return false;
  automaticPageTurnActive = false;
  if (!readerMenu) {
    startClipSelection();
  } else if (usesToolbarMenu()) {
    openOverlay(Overlay::Toolbar);
  } else {
    openReaderMenu();
  }
  return true;
}

bool EpubReaderActivity::isAtEndOfBook() const { return epub && currentSpineIndex >= epub->getSpineItemsCount(); }

void EpubReaderActivity::onReturnFromEndOfBook() {
  if (epub && epub->getSpineItemsCount() > 0) {
    currentSpineIndex = epub->getSpineItemsCount() - 1;
    nextPageNumber = 0;
    pendingPageJump = std::numeric_limits<uint16_t>::max();
  }
}

// A started build runs to the end of the chapter, one bounded step per idle loop frame, so the
// parser and its page LUT are freed soon (and a radio stopped for the build comes back with no
// parser alive). Not while the radio holds the heap, and not in a spine whose background build
// failed.
bool EpubReaderActivity::backgroundBuildWanted() const {
  return section && section->isBuilding() && !bleturner::holdsHeap() && currentSpineIndex != backgroundBuildFailedSpine;
}

bool EpubReaderActivity::skipLoopDelay() {
  // The main loop holds the render lock while querying this hint. It keeps yielding while a button
  // is down: a press that starts inside a long build step gets its second debounce sample ~5 ms
  // after the step instead of after a 10 ms sleep.
  return overlay == Overlay::None && !buildHeapPaused && backgroundBuildWanted();
}

void EpubReaderActivity::renderBook() {
  using Cache = std::remove_pointer_t<decltype(renderer.getFontCacheManager())>;
  struct Context {
    EpubReaderActivity& o;
    int margins[4]{};
    ReaderRenderSpec spec{};
    std::optional<std::string> path;
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    std::unique_ptr<Page> page;
    Cache* cache = nullptr;
    uint32_t read(uint8_t field) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderBookField>(field)) {
        case XP_UI_READER_BOOK_FIELD_SPINE: {
          return static_cast<uint32_t>(o.currentSpineIndex);
        }
        case XP_UI_READER_BOOK_FIELD_CACHED_SPINE: {
          return static_cast<uint32_t>(o.cachedSpineIndex);
        }
        case XP_UI_READER_BOOK_FIELD_CACHED_COUNT: {
          return static_cast<uint32_t>(o.cachedChapterTotalPageCount);
        }
        case XP_UI_READER_BOOK_FIELD_HAS_CACHED_OFFSET: {
          return static_cast<uint32_t>(o.cachedVisibleTextOffset.has_value());
        }
        case XP_UI_READER_BOOK_FIELD_CACHED_OFFSET: {
          return static_cast<uint32_t>(o.cachedVisibleTextOffset.value_or(0));
        }
        case XP_UI_READER_BOOK_FIELD_HAS_PENDING_PAGE: {
          return static_cast<uint32_t>(o.pendingPageJump.has_value());
        }
        case XP_UI_READER_BOOK_FIELD_PENDING_PAGE: {
          return static_cast<uint32_t>(o.pendingPageJump.value_or(0));
        }
        case XP_UI_READER_BOOK_FIELD_NEXT_PAGE: {
          return static_cast<uint32_t>(o.nextPageNumber);
        }
        case XP_UI_READER_BOOK_FIELD_HAS_PENDING_OFFSET: {
          return static_cast<uint32_t>(o.pendingOffsetJump.has_value());
        }
        case XP_UI_READER_BOOK_FIELD_PENDING_OFFSET: {
          return static_cast<uint32_t>(o.pendingOffsetJump.value_or(0));
        }
        case XP_UI_READER_BOOK_FIELD_PENDING_PERCENT: {
          return static_cast<uint32_t>(o.pendingPercentJump);
        }
        case XP_UI_READER_BOOK_FIELD_SPINE_PROGRESS: {
          uint32_t bits;
          memcpy(&bits, &o.pendingSpineProgress, sizeof(bits));
          return bits;
        }
        case XP_UI_READER_BOOK_FIELD_AUTOMATIC: {
          return static_cast<uint32_t>(o.automaticPageTurnActive);
        }
        case XP_UI_READER_BOOK_FIELD_SYNC_ERROR: {
          return static_cast<uint32_t>(o.pendingSyncSaveError);
        }
        case XP_UI_READER_BOOK_FIELD_PARTIAL_START_FAILED: {
          return static_cast<uint32_t>(o.partialRebuildStartFailed);
        }
        case XP_UI_READER_BOOK_FIELD_BUILD_POPUP: {
          return static_cast<uint32_t>(o.buildPopupPending);
        }
        case XP_UI_READER_BOOK_FIELD_BUILD_WIDTH: {
          return static_cast<uint32_t>(o.buildViewportWidth);
        }
        case XP_UI_READER_BOOK_FIELD_BUILD_HEIGHT: {
          return static_cast<uint32_t>(o.buildViewportHeight);
        }
        case XP_UI_READER_BOOK_FIELD_PAGE_STALE: {
          return static_cast<uint32_t>(o.pageBufferStale);
        }
        case XP_UI_READER_BOOK_FIELD_RETRY: {
          return static_cast<uint32_t>(o.pageLoadRetryCount);
        }
        case XP_UI_READER_BOOK_FIELD_CURRENT_OFFSET: {
          return static_cast<uint32_t>(o.currentPageVisibleOffset.value_or(0));
        }
        case XP_UI_READER_BOOK_FIELD_MARGIN_LEFT: {
          return static_cast<uint32_t>(o.currentPageLinkMarginLeft);
        }
        case XP_UI_READER_BOOK_FIELD_MARGIN_TOP: {
          return static_cast<uint32_t>(o.currentPageLinkMarginTop);
        }
        case XP_UI_READER_BOOK_FIELD_LAST_RENDER: {
          return static_cast<uint32_t>(o.lastRenderCompleteMs);
        }
        case XP_UI_READER_BOOK_FIELD_LAST_SAVED_SPINE: {
          return static_cast<uint32_t>(o.lastSavedSpineIndex);
        }
        case XP_UI_READER_BOOK_FIELD_LAST_SAVED_PAGE: {
          return static_cast<uint32_t>(o.lastSavedPage);
        }
        case XP_UI_READER_BOOK_FIELD_LAST_SAVED_COUNT: {
          return static_cast<uint32_t>(o.lastSavedPageCount);
        }
        case XP_UI_READER_BOOK_FIELD_SCREENSHOT: {
          return static_cast<uint32_t>(o.pendingScreenshot);
        }
        case XP_UI_READER_BOOK_FIELD_BOOKMARK_NOTICE: {
          return static_cast<uint32_t>(o.showBookmarkMessage);
        }
        case XP_UI_READER_BOOK_FIELD_BOOKMARK_REMOVED: {
          return static_cast<uint32_t>(o.bookmarkRemoved);
        }
        case XP_UI_READER_BOOK_FIELD_DICTIONARY_NOTICE: {
          return static_cast<uint32_t>(o.showDictionaryMessage);
        }
        case XP_UI_READER_BOOK_FIELD_FONT_NOTICE: {
          return static_cast<uint32_t>(o.showFontDamagedMessage);
        }
        case XP_UI_READER_BOOK_FIELD_FONT_NOTICE_MS: {
          return static_cast<uint32_t>(o.fontDamagedMessageTime);
        }
        case XP_UI_READER_BOOK_FIELD_CLIPPING_NOTICE: {
          return static_cast<uint32_t>(o.showClippingMessage);
        }
        case XP_UI_READER_BOOK_FIELD_OVERLAY: {
          return static_cast<uint32_t>(o.overlay);
        }
        case XP_UI_READER_BOOK_FIELD_STORED: {
          return static_cast<uint32_t>(o.overlayPageStored);
        }
      }
      return 0;
    }
    void write(uint8_t field, uint32_t value) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderBookField>(field)) {
        case XP_UI_READER_BOOK_FIELD_SPINE: {
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_CACHED_SPINE: {
          o.cachedSpineIndex = static_cast<decltype(o.cachedSpineIndex)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_CACHED_COUNT: {
          o.cachedChapterTotalPageCount = static_cast<decltype(o.cachedChapterTotalPageCount)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_HAS_CACHED_OFFSET: {
          assert(!value);
          o.cachedVisibleTextOffset.reset();
          break;
        }
        case XP_UI_READER_BOOK_FIELD_CACHED_OFFSET: {
          o.cachedVisibleTextOffset = value;
          break;
        }
        case XP_UI_READER_BOOK_FIELD_HAS_PENDING_PAGE: {
          assert(!value);
          o.pendingPageJump.reset();
          break;
        }
        case XP_UI_READER_BOOK_FIELD_PENDING_PAGE: {
          o.pendingPageJump = value;
          break;
        }
        case XP_UI_READER_BOOK_FIELD_NEXT_PAGE: {
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_HAS_PENDING_OFFSET: {
          assert(!value);
          o.pendingOffsetJump.reset();
          break;
        }
        case XP_UI_READER_BOOK_FIELD_PENDING_OFFSET: {
          o.pendingOffsetJump = value;
          break;
        }
        case XP_UI_READER_BOOK_FIELD_PENDING_PERCENT: {
          o.pendingPercentJump = static_cast<decltype(o.pendingPercentJump)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_SPINE_PROGRESS: {
          memcpy(&o.pendingSpineProgress, &value, sizeof(value));
          break;
        }
        case XP_UI_READER_BOOK_FIELD_AUTOMATIC: {
          o.automaticPageTurnActive = static_cast<decltype(o.automaticPageTurnActive)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_SYNC_ERROR: {
          o.pendingSyncSaveError = static_cast<decltype(o.pendingSyncSaveError)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_PARTIAL_START_FAILED: {
          o.partialRebuildStartFailed = static_cast<decltype(o.partialRebuildStartFailed)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_BUILD_POPUP: {
          o.buildPopupPending = static_cast<decltype(o.buildPopupPending)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_BUILD_WIDTH: {
          o.buildViewportWidth = static_cast<decltype(o.buildViewportWidth)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_BUILD_HEIGHT: {
          o.buildViewportHeight = static_cast<decltype(o.buildViewportHeight)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_PAGE_STALE: {
          o.pageBufferStale = value != 0;
          break;
        }
        case XP_UI_READER_BOOK_FIELD_RETRY: {
          o.pageLoadRetryCount = static_cast<decltype(o.pageLoadRetryCount)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_CURRENT_OFFSET: {
          o.currentPageVisibleOffset = value;
          break;
        }
        case XP_UI_READER_BOOK_FIELD_MARGIN_LEFT: {
          o.currentPageLinkMarginLeft = static_cast<decltype(o.currentPageLinkMarginLeft)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_MARGIN_TOP: {
          o.currentPageLinkMarginTop = static_cast<decltype(o.currentPageLinkMarginTop)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_LAST_RENDER: {
          o.lastRenderCompleteMs = static_cast<decltype(o.lastRenderCompleteMs)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_LAST_SAVED_SPINE: {
          o.lastSavedSpineIndex = static_cast<decltype(o.lastSavedSpineIndex)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_LAST_SAVED_PAGE: {
          o.lastSavedPage = static_cast<decltype(o.lastSavedPage)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_LAST_SAVED_COUNT: {
          o.lastSavedPageCount = static_cast<decltype(o.lastSavedPageCount)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_SCREENSHOT: {
          o.pendingScreenshot = static_cast<decltype(o.pendingScreenshot)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_BOOKMARK_NOTICE: {
          o.showBookmarkMessage = static_cast<decltype(o.showBookmarkMessage)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_BOOKMARK_REMOVED: {
          o.bookmarkRemoved = static_cast<decltype(o.bookmarkRemoved)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_DICTIONARY_NOTICE: {
          o.showDictionaryMessage = static_cast<decltype(o.showDictionaryMessage)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_FONT_NOTICE: {
          o.showFontDamagedMessage = static_cast<decltype(o.showFontDamagedMessage)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_FONT_NOTICE_MS: {
          o.fontDamagedMessageTime = static_cast<decltype(o.fontDamagedMessageTime)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_CLIPPING_NOTICE: {
          o.showClippingMessage = static_cast<decltype(o.showClippingMessage)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_OVERLAY: {
          o.overlay = static_cast<decltype(o.overlay)>(value);
          break;
        }
        case XP_UI_READER_BOOK_FIELD_STORED: {
          o.overlayPageStored = static_cast<decltype(o.overlayPageStored)>(value);
          break;
        }
      }
    }
    uint32_t query(uint8_t query) {
      switch (static_cast<XpUiReaderBookQuery>(query)) {
        case XP_UI_READER_BOOK_QUERY_HAS_BOOK: {
          return bool(o.epub);
        }
        case XP_UI_READER_BOOK_QUERY_SPINE_COUNT: {
          return o.epub->getSpineItemsCount();
        }
        case XP_UI_READER_BOOK_QUERY_HAS_SECTION: {
          return bool(o.section);
        }
        case XP_UI_READER_BOOK_QUERY_MARGIN_TOP: {
          return margins[0];
        }
        case XP_UI_READER_BOOK_QUERY_MARGIN_RIGHT: {
          return margins[1];
        }
        case XP_UI_READER_BOOK_QUERY_MARGIN_BOTTOM: {
          return margins[2];
        }
        case XP_UI_READER_BOOK_QUERY_MARGIN_LEFT: {
          return margins[3];
        }
        case XP_UI_READER_BOOK_QUERY_SCREEN_MARGIN: {
          return SETTINGS.screenMargin;
        }
        case XP_UI_READER_BOOK_QUERY_STATUS_HEIGHT: {
          return UITheme::getInstance().getStatusBarHeight();
        }
        case XP_UI_READER_BOOK_QUERY_PROGRESS_HEIGHT: {
          return UITheme::getInstance().getProgressBarHeight();
        }
        case XP_UI_READER_BOOK_QUERY_STATUS_MARGIN: {
          return UITheme::getInstance().getMetrics().statusBarVerticalMargin;
        }
        case XP_UI_READER_BOOK_QUERY_WIDTH: {
          return o.renderer.getScreenWidth();
        }
        case XP_UI_READER_BOOK_QUERY_HEIGHT: {
          return o.renderer.getScreenHeight();
        }
        case XP_UI_READER_BOOK_QUERY_PARTIAL: {
          return o.section->isPartial();
        }
        case XP_UI_READER_BOOK_QUERY_BUILDING: {
          return o.section->isBuilding();
        }
        case XP_UI_READER_BOOK_QUERY_COMPLETE: {
          return o.section->isBuildComplete();
        }
        case XP_UI_READER_BOOK_QUERY_PAGE_COUNT: {
          return o.section->pageCount;
        }
        case XP_UI_READER_BOOK_QUERY_PAGE: {
          return o.section->currentPage;
        }
        case XP_UI_READER_BOOK_QUERY_HAS_ANCHOR: {
          return !o.pendingAnchor.empty();
        }
        case XP_UI_READER_BOOK_QUERY_HAS_HTML: {
          return o.section->hasHtmlCache();
        }
        case XP_UI_READER_BOOK_QUERY_FIND_ANCHOR: {
          const auto p = o.section->findAnchor(o.pendingAnchor);
          return p ? 0x10000u | *p : 0;
        }
        case XP_UI_READER_BOOK_QUERY_PAGE_FOR_ANCHOR: {
          const auto p = o.section->getPageForAnchor(o.pendingAnchor);
          return p ? 0x10000u | *p : 0;
        }
        case XP_UI_READER_BOOK_QUERY_HAS_TTF: {
          return !o.renderer.getTtfFonts().empty();
        }
        case XP_UI_READER_BOOK_QUERY_NOW: {
          return millis();
        }
        case XP_UI_READER_BOOK_QUERY_POPUP_ACTIVE: {
          return o.overlayPopup.isActive();
        }
        case XP_UI_READER_BOOK_QUERY_USES_TOOLBAR: {
          return o.usesToolbarMenu();
        }
        case XP_UI_READER_BOOK_QUERY_HAS_PAGE: {
          return bool(page);
        }
        case XP_UI_READER_BOOK_QUERY_PAGE_OFFSET: {
          return page->visibleTextOffset;
        }
        case XP_UI_READER_BOOK_QUERY_ESTIMATED_COUNT: {
          return o.section->estimatedTotalPages();
        }
      }
      return 0;
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderBookCommand>(command)) {
        case XP_UI_READER_BOOK_COMMAND_SAMPLE_FONT_CACHE: {
          assert(!cache);
          cache = o.renderer.getFontCacheManager();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_TAKE_FONT_NOTICE: {
          return SdCardFont::takeCorruptGlyphNotice();
        }
        case XP_UI_READER_BOOK_COMMAND_CLEAR_METADATA: {
          o.currentPageMetadata.clear();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_RADIO_SETTLE: {
          bleturner::settleStart();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SETTLE_OVERLAY: {
          o.settleOverlayRefresh();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SAMPLE_MARGINS: {
          o.renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SET_MARGIN: {
          assert(a >= 0 && a < 4);
          margins[a] = b;
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SET_SPEC: {
          spec = SETTINGS.readerRenderSpec(o.buildViewportWidth, o.buildViewportHeight);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_GET_PATH: {
          assert(!path);
          path.emplace(o.epub->getSpineItem(o.currentSpineIndex).href);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DROP_PATH: {
          path.reset();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_LOADING: {
          LOG_DBG("ERS", "Loading file: %s, index: %d", path->c_str(), o.currentSpineIndex);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_NEW_SECTION: {
          o.section = std::unique_ptr<Section>(new Section(o.epub, o.currentSpineIndex, o.renderer));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOAD_CACHE: {
          return o.section->loadSectionFile(spec);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_PARTIAL: {
          LOG_DBG("ERS", "Partial cache found (%d pages), resuming build...", o.section->pageCount);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_NO_CACHE: {
          LOG_DBG("ERS", "Cache not found, building...");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_COUNTER: {
          o.renderRefreshCounter() = a;
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_ROOM: {
          return o.makeRoomForChapterBuild();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DROP_SECTION: {
          o.section.reset();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOAN_BEGIN: {
          assert(!loan);
          loan.emplace(o.renderer);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOAN_END: {
          assert(loan);
          loan->end();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOAN_DROP: {
          loan.reset();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_CREATE_ALL: {
          return o.section->createSectionFile(spec, [owner = &o] {
            if (owner->renderer.hasFrameBuffer()) GUI.drawPopup(owner->renderer, tr(STR_INDEXING));
          });
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_PERSIST_FAILED: {
          LOG_ERR("ERS", "Failed to persist page data to SD");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_DEFERRED: {
          LOG_DBG("ERS", "Partial covers target %d of %d; deferring extension build", a, o.section->pageCount);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_CUMULATIVE: {
          return o.epub->getCumulativeSpineItemSize(a);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_RELEASE_FONT_CACHE: {
          if (cache) cache->releaseSdFontCaches();
          cache = nullptr;
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_HEAP: {
          LOG_DBG("ERS", "Heap before section build: %u (max block %u)", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap());
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_START_WITH_POPUP: {
          return o.section->startBuild(spec, [owner = &o] { owner->showBuildPopup(); });
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_START_FAILED: {
          LOG_ERR("ERS", "Failed to start section build");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_REACHED_OFFSET: {
          return o.section->buildReachedVisibleTextOffset(static_cast<uint32_t>(a));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SHOW_BUILD_POPUP: {
          o.showBuildPopup();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_BUILD_MORE: {
          return o.section->buildSomeMore(a);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_STEP_FAILED: {
          LOG_ERR("ERS", "Failed during incremental section build");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_CACHE_FOUND: {
          LOG_DBG("ERS", "Cache found, skipping build...");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_PAGE: {
          o.section->currentPage = a;
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_MAP_OFFSET: {
          const auto p = o.section->getPageForVisibleTextOffset(static_cast<uint32_t>(a));
          return p ? 0x10000u | *p : 0;
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_CLEAR_DEFERRED: {
          o.clearDeferredReposition();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_ANCHOR: {
          LOG_DBG("ERS", "Resolved anchor '%s' to page %d", o.pendingAnchor.c_str(), a);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_CLEAR_ANCHOR: {
          o.pendingAnchor.clear();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_START_PARTIAL: {
          return o.section->startBuild(spec);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_PARTIAL_START_FAILED: {
          LOG_ERR("ERS", "Failed to start partial extension build");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_APPLY_DEFERRED: {
          o.applyDeferredReposition();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_CLEAR_SCREEN: {
          o.renderer.clearScreen();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_EMPTY: {
          LOG_DBG("ERS", "No pages to render");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_STATUS_BAR: {
          o.renderStatusBar();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DISPLAY: {
          o.renderer.displayBuffer();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_BOUNDS: {
          LOG_DBG("ERS", "Page out of bounds: %d (max %d)", o.section->currentPage, o.section->pageCount);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_BOOKMARK_FLAG: {
          o.updateBookmarkFlag();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOAD_PAGE: {
          assert(!page);
          page = o.section->loadPage(o.section->currentPage);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DROP_PAGE: {
          page.reset();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_ASSIGN_METADATA: {
          return o.currentPageMetadata.assign(*page);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_PAGE_LOAD_FAILED: {
          LOG_ERR("ERS", "Failed to load page from SD - clearing section cache");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_METADATA_FAILED: {
          LOG_ERR("ERS", "Failed to copy page metadata - clearing section cache");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_ABANDON_BUILD: {
          o.section->abandonBuild();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_CLEAR_CACHE: {
          o.section->clearCache();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_RETRY_FAILED: {
          LOG_ERR("ERS", "Page load retry limit reached, aborting");
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_UPDATE: {
          o.requestUpdate();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DISCARD_OVERLAY: {
          o.discardOverlayPage();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_RENDER_CONTENTS: {
          o.renderContents(std::move(page), margins[0], margins[1], margins[2], margins[3]);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_LOG_RENDERED: {
          LOG_DBG("ERS", "Rendered page in %dms", millis() - static_cast<uint32_t>(a));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_MARK_RENDERED: {
          o.markPageRendered();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SAVE_PROGRESS: {
          return o.saveProgress(o.currentSpineIndex, o.section->currentPage, o.section->estimatedTotalPages());
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_SCREENSHOT: {
          ScreenshotUtil::takeScreenshot(o.renderer);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_STORE: {
          return o.renderer.storeBwBuffer();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_RENDER_OVERLAY: {
          o.renderOverlay();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_RENDER_POPUP: {
          o.overlayPopup.render(o.renderer);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_PUSH_REFRESH: {
          o.pushOverlayRefresh();
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_BACK_HINTS: {
          const auto labels = o.mappedInput.mapLabels(tr(STR_BACK), "", "", "");
          GUI.drawButtonHints(o.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_SYNC: {
          GUI.drawPopup(o.renderer, tr(STR_SAVE_PROGRESS_FAILED));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_RADIO_ERROR: {
          GUI.drawPopup(o.renderer, tr(STR_BT_BUILD_REFUSED));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_BUILD_ERROR: {
          GUI.drawPopup(o.renderer, tr(STR_INDEX_FAILED));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_INDEX: {
          GUI.drawPopup(o.renderer, tr(STR_INDEXING));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_BOOKMARK_REMOVED: {
          GUI.drawPopup(o.renderer, tr(STR_BOOKMARK_REMOVED));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_BOOKMARK_ADDED: {
          GUI.drawPopup(o.renderer, tr(STR_BOOKMARK_ADDED));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_DICTIONARY: {
          GUI.drawPopup(o.renderer, tr(STR_DICT_NO_DICT_SET));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_FONT: {
          GUI.drawPopup(o.renderer, tr(STR_FONT_DAMAGED));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_POPUP_CLIPPING: {
          GUI.drawPopup(o.renderer, I18N.get(o.clippingMessage));
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DRAW_EMPTY: {
          o.renderer.drawCenteredText(UI_12_FONT_ID, a, tr(STR_EMPTY_CHAPTER), true, EpdFontFamily::BOLD);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DRAW_BOUNDS: {
          o.renderer.drawCenteredText(UI_12_FONT_ID, a, tr(STR_OUT_OF_BOUNDS), true, EpdFontFamily::BOLD);
          break;
        }
        case XP_UI_READER_BOOK_COMMAND_DRAW_LOAD_ERROR: {
          o.renderer.drawCenteredText(UI_12_FONT_ID, a, tr(STR_PAGE_LOAD_ERROR), true, EpdFontFamily::BOLD);
          break;
        }
      }
      return 0;
    }
  } context{*this, {}, {}, {}, {}, {}, nullptr};
  const XpUiReaderBookServices services{
      &context, [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t q) { return static_cast<Context*>(p)->query(q); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); }};
  xp_ui_reader_book_render(&services);
  assert(!context.path && !context.loan && !context.page && !context.cache);
}

void EpubReaderActivity::onEndOfBookRendered() {
  automaticPageTurnActive = false;
  if (pendingSyncSaveError) {
    pendingSyncSaveError = false;
    GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  }
}

bool EpubReaderActivity::applyDeferredReposition() { return runNavigationPolicy(XP_UI_READER_NAV_OPERATION_DEFERRED); }

void EpubReaderActivity::clearDeferredReposition() { runNavigationPolicy(XP_UI_READER_NAV_OPERATION_CLEAR); }

bool EpubReaderActivity::saveProgress(int spineIndex, int currentPage, int pageCount) {
  return runNavigationPolicy(XP_UI_READER_NAV_OPERATION_SAVE, spineIndex, currentPage, pageCount);
}

void EpubReaderActivity::rememberCurrentContentOffset() { runNavigationPolicy(XP_UI_READER_NAV_OPERATION_REMEMBER); }

void EpubReaderActivity::renderContents(std::unique_ptr<Page> page, const int orientedMarginTop,
                                        const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) {
  (void)orientedMarginRight;
  struct Context {
    EpubReaderActivity& owner;
    Page& page;
    int font, left, top;
    bool images = false;
    struct ImageGuard {
      ~ImageGuard() { ImageBlock::releaseRenderCache(); }
    } imageGuard;
    std::optional<FontCacheManager::PrewarmScope> prewarm;
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
    struct StripFrame {
      EpubReaderActivity& owner;
      ReaderStripPainter painter;
      XpPagePlan plan{};
      XpDisplaySource source{};
      bool active = false;
      ~StripFrame() {
        if (active) display.setFrameSource(nullptr);
        owner.stripBuffers.setSourceLoanActive(false);
        owner.renderer.usePagePlan(nullptr, nullptr, 0);
        ImageBlock::setRenderReadWindow(nullptr, 0);
      }
    } strips;
#endif
    std::unique_ptr<uint8_t[]> lsb, msb, scratch;
    uint8_t* borrowedScratch = nullptr;
    unsigned long times[10]{};
    Context(EpubReaderActivity& owner, Page& page, int left, int top, int bottom)
        : owner(owner),
          page(page),
          font(SETTINGS.getReaderFontId()),
          left(left),
          top(top)
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          ,
          strips{owner,
                 {owner.renderer, page, font, left, top, owner.renderer.getScreenHeight() - bottom - 8,
                  display.isInverted(), &owner,
                  [](void* raw, const Page& page, int font, int left, int top) {
                    static_cast<EpubReaderActivity*>(raw)->drawClippingHighlights(page, font, top, left);
                  }}}
#endif
    {
      (void)bottom;
    }
    uint8_t* buffer(uint8_t id) {
      switch (static_cast<XpUiReaderRenderBuffer>(id)) {
        case XP_UI_READER_BUFFER_LSB:
          return lsb.get();
        case XP_UI_READER_BUFFER_MSB:
          return msb.get();
        case XP_UI_READER_BUFFER_SCRATCH:
          return borrowedScratch ? borrowedScratch : scratch.get();
      }
      return nullptr;
    }
    uint32_t read(uint8_t query) {
      auto& renderer = owner.renderer;
      switch (static_cast<XpUiReaderRenderQuery>(query)) {
        case XP_UI_READER_RENDER_NOW:
          return millis();
        case XP_UI_READER_RENDER_HAS_IMAGES:
          images = page.hasImages();
          return images;
        case XP_UI_READER_RENDER_IMAGES_NEED_DECODE:
          return page.hasImagesNeedingDecode();
        case XP_UI_READER_RENDER_TAKE_FORCED: {
          return owner.takeForcedRefresh();
        }
        case XP_UI_READER_RENDER_COUNTER:
          return owner.renderRefreshCounter();
        case XP_UI_READER_RENDER_FREQUENCY:
          return SETTINGS.getRefreshFrequency();
        case XP_UI_READER_RENDER_TEXT_AA:
          return SETTINGS.textAntiAliasing;
        case XP_UI_READER_RENDER_IS_A4:
          return BoardConfig::isEegoA4();
        case XP_UI_READER_RENDER_IS_X3:
          return gpio.deviceIsX3();
        case XP_UI_READER_RENDER_IS_UC8279:
          return display.getController() == HalDisplay::Controller::UC8279;
        case XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED:
          return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
        case XP_UI_READER_RENDER_OVERLAY_CAPS:
        case XP_UI_READER_RENDER_ABSOLUTE_CAPS: {
          const auto caps = renderer.grayscaleCapabilities(query == XP_UI_READER_RENDER_ABSOLUTE_CAPS
                                                               ? HalDisplay::GrayscaleMode::Absolute
                                                               : HalDisplay::GrayscaleMode::Overlay);
          return (caps.stripUploads ? 1u : 0u) | (caps.base == HalDisplay::GrayscaleBase::Combined ? 2u : 0u) |
                 (caps.asyncBase ? 4u : 0u);
        }
        case XP_UI_READER_RENDER_STRIP_BOARD:
          return display.getController() == HalDisplay::Controller::UC8253 ||
                 (BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4 &&
                  display.getController() == HalDisplay::Controller::SSD1677);
        case XP_UI_READER_RENDER_ELEMENTS:
          return page.elementCount();
        case XP_UI_READER_RENDER_WIDTH_BYTES:
          return renderer.getDisplayWidthBytes();
        case XP_UI_READER_RENDER_HEIGHT:
          return renderer.getDisplayHeight();
        case XP_UI_READER_RENDER_FREE_HEAP:
          return ESP.getFreeHeap();
        case XP_UI_READER_RENDER_MAX_BLOCK:
          return ESP.getMaxAllocHeap();
      }
      return 0;
    }
    void log(uint32_t id) const {
      const auto* t = times;
      switch (static_cast<XpUiReaderRenderLog>(id)) {
        case XP_UI_READER_RENDER_STRIP_OOM:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          LOG_ERR(
              "ERS", "OOM: reader strips (%u bytes); using framebuffer",
              static_cast<unsigned>(ReaderStripBuffers::SCRATCH_BYTES + ReaderStripBuffers::COMMAND_BYTES +
                                    sizeof(XpPageRowBounds) * ReaderStripBuffers::MAX_ELEMENTS + (images ? 4096 : 0)));
#endif
          break;
        case XP_UI_READER_RENDER_ABSOLUTE_FAILED:
          LOG_ERR("ERS", "Could not start absolute image page; displaying B/W");
          break;
        case XP_UI_READER_RENDER_ABSOLUTE_QUALITY:
          LOG_DBG("ERS", "UC8279 image page: absolute quality waveform");
          break;
        case XP_UI_READER_RENDER_SCRATCH_OOM:
          LOG_ERR("ERS", "OOM: grayscale strip scratch (%d bytes); skipping AA this page",
                  owner.renderer.getDisplayWidthBytes() * 80);
          break;
        case XP_UI_READER_RENDER_STORE_FAILED:
          LOG_ERR("ERS", "Failed to store BW buffer for grayscale render; skipping grayscale this page");
          break;
        case XP_UI_READER_RENDER_TIMING_ASYNC:
          LOG_DBG("ERS",
                  "Page render (tiled async): prewarm=%lums bw_render=%lums display=%lums gray_render=%lums "
                  "wait=%lums gray_write=%lums gray_display=%lums cleanup=%lums total=%lums (planes buffered: %d)",
                  t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6],
                  t[8] - t[7], t[8] - t[0], msb ? 2 : 1);
          break;
        case XP_UI_READER_RENDER_TIMING_TILED:
          LOG_DBG("ERS",
                  "Page render (tiled): prewarm=%lums bw_render=%lums display=%lums gray_lsb=%lums "
                  "gray_msb=%lums gray_display=%lums cleanup=%lums total=%lums",
                  t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6],
                  t[8] - t[0]);
          break;
        case XP_UI_READER_RENDER_TIMING_GRAY:
          LOG_DBG("ERS",
                  "Page render: prewarm=%lums bw_render=%lums display=%lums bw_store=%lums "
                  "gray_lsb=%lums gray_msb=%lums gray_display=%lums bw_restore=%lums total=%lums",
                  t[1] - t[0], t[2] - t[1], t[3] - t[2], t[4] - t[3], t[5] - t[4], t[6] - t[5], t[7] - t[6],
                  t[8] - t[7], t[9] - t[0]);
          break;
        case XP_UI_READER_RENDER_TIMING_BW:
          LOG_DBG("ERS", "Page render: prewarm=%lums bw_render=%lums display=%lums total=%lums", t[1] - t[0],
                  t[2] - t[1], t[3] - t[2], t[4] - t[0]);
          break;
      }
    }
    uint32_t execute(const XpUiReaderRenderOp& op) {
      auto& renderer = owner.renderer;
      const auto refresh = op.mode == XP_UI_HALF ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;
      switch (static_cast<XpUiReaderRenderOperation>(op.kind)) {
        case XP_UI_READER_RENDER_IMAGE_RESET:
          ImageBlock::clearRenderFailures();
          break;
        case XP_UI_READER_RENDER_PREWARM_BEGIN:
          prewarm.emplace(*renderer.getFontCacheManager());
          break;
        case XP_UI_READER_RENDER_PAGE:
          page.render(renderer, font, left, top);
          break;
        case XP_UI_READER_RENDER_STATUS:
          owner.renderStatusBar();
          break;
        case XP_UI_READER_RENDER_PREWARM_END:
          prewarm->endScanAndPrewarm();
          break;
        case XP_UI_READER_RENDER_HIGHLIGHTS:
          owner.drawClippingHighlights(page, font, top, left);
          break;
        case XP_UI_READER_RENDER_IMAGES:
          page.renderImages(renderer, font, left, top);
          break;
        case XP_UI_READER_RENDER_PLACEHOLDERS:
          page.renderWithImagePlaceholders(renderer, font, left, top);
          break;
        case XP_UI_READER_RENDER_CLEAR:
          renderer.clearScreen(op.value);
          break;
        case XP_UI_READER_RENDER_DISPLAY:
          renderer.displayBuffer(refresh);
          break;
        case XP_UI_READER_RENDER_DISPLAY_ASYNC:
          renderer.displayBufferAsync(refresh);
          break;
        case XP_UI_READER_RENDER_GRAY_BASE:
          renderer.displayGrayscaleBase(refresh);
          break;
        case XP_UI_READER_RENDER_ABSOLUTE_BASE:
          return renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute, refresh);
        case XP_UI_READER_RENDER_PRECONDITION:
          renderer.preconditionGrayscale();
          break;
        case XP_UI_READER_RENDER_SET_COUNTER:
          owner.renderRefreshCounter() = static_cast<int32_t>(op.value);
          break;
        case XP_UI_READER_RENDER_PREPARE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          return owner.stripBuffers.prepare(renderer.getDisplayWidthBytes(), op.value != 0);
#else
          return false;
#endif
        case XP_UI_READER_RENDER_IMAGE_WINDOW:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          ImageBlock::setRenderReadWindow(owner.stripBuffers.imageWindow.get(),
                                          owner.stripBuffers.imageWindow ? 4096 : 0);
#endif
          break;
        case XP_UI_READER_RENDER_RECORD_BEGIN:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          strips.plan = {owner.stripBuffers.commands.get(), ReaderStripBuffers::COMMAND_BYTES, 0, 0};
          renderer.usePagePlan(&strips.plan, owner.stripBuffers.bounds.get(), page.elementCount(), true);
#endif
          break;
        case XP_UI_READER_RENDER_RECORD_END:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          renderer.usePagePlan(strips.plan.valid ? &strips.plan : nullptr, owner.stripBuffers.bounds.get(),
                               page.elementCount());
          return strips.plan.valid;
#else
          return false;
#endif
        case XP_UI_READER_RENDER_SET_SOURCE:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          strips.source = {sizeof(XpDisplaySource),          &strips.painter,
                           owner.stripBuffers.scratch.get(), op.value,
                           ReaderStripBuffers::ROWS,         renderer.getDisplayWidth(),
                           &ReaderStripPainter::paint};
          strips.active = display.setFrameSource(&strips.source);
          owner.stripBuffers.setSourceLoanActive(strips.active);
          return strips.active;
#else
          return false;
#endif
        case XP_UI_READER_RENDER_CLEAR_PLAN:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          renderer.usePagePlan(nullptr, nullptr, 0);
#endif
          break;
        case XP_UI_READER_RENDER_CLEAR_WINDOW:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          ImageBlock::setRenderReadWindow(nullptr, 0);
#endif
          break;
        case XP_UI_READER_RENDER_RELEASE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          owner.stripBuffers.releaseIdle();
#endif
          break;
        case XP_UI_READER_RENDER_ALLOCATE:
          switch (static_cast<XpUiReaderRenderBuffer>(op.buffer)) {
            case XP_UI_READER_BUFFER_LSB:
              lsb = makeUniqueNoThrow<uint8_t[]>(op.value);
              return lsb != nullptr;
            case XP_UI_READER_BUFFER_MSB:
              msb = makeUniqueNoThrow<uint8_t[]>(op.value);
              return msb != nullptr;
            case XP_UI_READER_BUFFER_SCRATCH:
              scratch = makeUniqueNoThrow<uint8_t[]>(op.value);
              return scratch != nullptr;
          }
          return false;
        case XP_UI_READER_RENDER_BORROW_SCRATCH:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          borrowedScratch = owner.stripBuffers.scratch.get();
          return borrowedScratch != nullptr;
#else
          return false;
#endif
        case XP_UI_READER_RENDER_MODE:
          switch (op.mode) {
            case 0:
              renderer.setRenderMode(GfxRenderer::BW);
              break;
            case 1:
              renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
              break;
            case 2:
              renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
              break;
          }
          break;
        case XP_UI_READER_RENDER_STRIP_BEGIN:
          renderer.beginStripTarget(buffer(op.buffer) + op.offset, op.y, op.rows);
          break;
        case XP_UI_READER_RENDER_STRIP_END:
          renderer.endStripTarget();
          break;
        case XP_UI_READER_RENDER_WAIT:
          renderer.waitRefreshComplete();
          break;
        case XP_UI_READER_RENDER_WRITE_PLANE:
          renderer.writeGrayscalePlaneStrip(op.mode == 1, buffer(op.buffer), op.y, op.rows);
          break;
        case XP_UI_READER_RENDER_GRAY_DISPLAY:
          renderer.displayGrayBuffer();
          break;
        case XP_UI_READER_RENDER_CLEANUP:
          renderer.cleanupGrayscaleWithFrameBuffer();
          break;
        case XP_UI_READER_RENDER_STORE_BW:
          return renderer.storeBwBuffer();
        case XP_UI_READER_RENDER_RESTORE_BW:
          renderer.restoreBwBuffer();
          break;
        case XP_UI_READER_RENDER_COPY_LSB:
          renderer.copyGrayscaleLsbBuffers();
          break;
        case XP_UI_READER_RENDER_COPY_MSB:
          renderer.copyGrayscaleMsbBuffers();
          break;
        case XP_UI_READER_RENDER_LOG:
          log(op.value);
          break;
        case XP_UI_READER_RENDER_MARK:
          times[op.value] = millis();
          break;
      }
      return 0;
    }
  } context{*this, *page, orientedMarginLeft, orientedMarginTop, orientedMarginBottom};
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
  static_assert(ReaderStripBuffers::ROWS == 80 && ReaderStripBuffers::MAX_ELEMENTS == 512);
  constexpr uint8_t stripEnabled = true;
#else
  constexpr uint8_t stripEnabled = false;
#endif
  const XpUiReaderRenderServices services{
      &context, [](void* raw, uint8_t query) -> uint32_t { return static_cast<Context*>(raw)->read(query); },
      [](void* raw, const XpUiReaderRenderOp* op) -> uint32_t { return static_cast<Context*>(raw)->execute(*op); }};
  xp_ui_reader_render_page(&services, stripEnabled);
}

int EpubReaderActivity::clippingAtPoint(const Page& page, const int x, const int y) const {
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int fontId = SETTINGS.getReaderFontId();
  const uint32_t signature =
      readerRenderSpecSignature(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight));
  uint16_t wordIndex = 0;
  for (uint16_t elementIndex = 0; elementIndex < page.elementCount(); ++elementIndex) {
    const auto line = page.element(elementIndex);
    if (line.getTag() != TAG_PageLine) continue;
    const auto& textBlock = line.textBlock();
    const auto* block = &textBlock;
    if (!block || !block->valid()) continue;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      const int width = renderer.getTextAdvanceX(clippingWordFont(renderer, fontId, *block, i), text, style,
                                                 block->getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      const uint16_t index = wordIndex++;
      const Rect rect = clippingWordRect(renderer, fontId, line, *block, i, top + SETTINGS.screenMargin,
                                         left + SETTINGS.screenMargin, width);
      if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height) continue;
      const auto range = block->wordSourceRange(i);
      for (size_t j = 0; j < CLIPPINGS.clippingCount(); ++j) {
        if (clippingContainsWord(*CLIPPINGS.clippingAt(j), currentSpineIndex, section->currentPage, section->pageCount,
                                 signature, index, range.start, range.end))
          return static_cast<int>(j);
      }
    }
  }
  return -1;
}

void EpubReaderActivity::drawClippingHighlights(const Page& page, const int fontId, const int orientedMarginTop,
                                                const int orientedMarginLeft) const {
  if (!section || !CLIPPINGS.hasClippings() || section->currentPage < 0 || section->currentPage >= section->pageCount) {
    return;
  }

  size_t chapterClippings = 0;
  for (size_t i = 0; i < CLIPPINGS.clippingCount(); ++i) {
    if (CLIPPINGS.clippingAt(i)->spineIndex == currentSpineIndex) ++chapterClippings;
  }
  if (!chapterClippings) return;

  const auto started = millis();
  size_t highlightedWords = 0;
  const uint16_t currentPage = static_cast<uint16_t>(section->currentPage);
  const uint32_t layoutSignature =
      readerRenderSpecSignature(SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight));
  const auto isHighlighted = [&](const uint16_t wordIndex, const TextBlock::SourceRange range) {
    // ponytail: scan at most 256 C3 / 1024 S3 clippings per word; index by chapter if this becomes slow.
    for (size_t index = 0; index < CLIPPINGS.clippingCount(); ++index) {
      const Clipping& clipping = *CLIPPINGS.clippingAt(index);
      if (clippingContainsWord(clipping, static_cast<uint16_t>(currentSpineIndex), currentPage, section->pageCount,
                               layoutSignature, wordIndex, range.start, range.end))
        return true;
    }
    return false;
  };

  const auto fillHighlight = [&](const int x, const int y, const int width, const int height) {
    if (BoardConfig::isEegoA4() && renderer.getRenderMode() != GfxRenderer::BW) {
      // Gray planes start at zero and use set bits for ink, opposite to B/W.
      for (int py = y + (y & 1); py < y + height; py += 2) {
        for (int px = x + (x & 1); px < x + width; px += 2) renderer.drawPixel(px, py, false);
      }
    } else {
      renderer.fillRectDither(x, y, width, height, Color::LightGray);
    }
  };

  uint16_t pageWordIndex = 0;
  for (uint16_t elementIndex = 0; elementIndex < page.elementCount(); ++elementIndex) {
    const auto line = page.element(elementIndex);
    if (line.getTag() != TAG_PageLine) continue;
    const auto& textBlock = line.textBlock();
    const auto* block = &textBlock;
    if (!block || !block->valid()) continue;
    bool hasPreviousHighlight = false;
    int previousHighlightX = 0;
    int previousHighlightWidth = 0;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      const char* text = block->wordText(i);
      if (!clippingText::hasVisibleText(text)) continue;
      const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
      int width = renderer.getTextAdvanceX(clippingWordFont(renderer, fontId, *block, i), text, style,
                                           block->getBlockStyle().characterSpacing);
      if (width <= 0) continue;
      const uint16_t wordIndex = pageWordIndex++;
      if (!isHighlighted(wordIndex, block->wordSourceRange(i))) {
        hasPreviousHighlight = false;
        continue;
      }

      const Rect rect =
          clippingWordRect(renderer, fontId, line, *block, i, orientedMarginTop, orientedMarginLeft, width);
      const int x = rect.x;
      const int y = rect.y;
      width = rect.width;
      if (width > 0) {
        const int highlightRight = x + width;
        const int previousHighlightRight = previousHighlightX + previousHighlightWidth;
        if (hasPreviousHighlight && previousHighlightRight < x) {
          fillHighlight(previousHighlightRight, y, x - previousHighlightRight, renderer.getLineHeight(fontId));
        } else if (hasPreviousHighlight && highlightRight < previousHighlightX) {
          fillHighlight(highlightRight, y, previousHighlightX - highlightRight, renderer.getLineHeight(fontId));
        }
        fillHighlight(x, y, width, renderer.getLineHeight(fontId));
        previousHighlightX = x;
        previousHighlightWidth = width;
        hasPreviousHighlight = true;
        ++highlightedWords;
      } else {
        hasPreviousHighlight = false;
      }
    }
  }
  LOG_DBG("CLIP", "Highlight pass: chapter_clips=%u highlighted_words=%u time=%lums",
          static_cast<unsigned>(chapterClippings), static_cast<unsigned>(highlightedWords), millis() - started);
}

void EpubReaderActivity::renderStatusBar() const {
  const int currentPage = section ? section->currentPage + 1 : 1;
  const float pageCount = section ? section->estimatedTotalPages() : 1;
  const float sectionChapterProg = (pageCount > 0) ? (static_cast<float>(currentPage) / pageCount) : 0;
  const float bookProgress = epub ? (epub->calculateProgress(currentSpineIndex, sectionChapterProg) * 100) : 0;

  std::string title;
  int textYOffset = 0;
  const auto sb = SETTINGS.statusBarSpec();

  const auto note = linkNoteTitle.draw(sb.showsTitle(), bleturner::linkNote());
  if (note == bleturner::LinkNote::Connected) {
    const auto peer = bleturner::linked();
    char connected[64];
    snprintf(connected, sizeof(connected), tr(STR_BT_CONNECTED_TO), peer.name[0] != '\0' ? peer.name : peer.addr);
    title = connected;
  } else if (note == bleturner::LinkNote::LowMemory) {
    title = tr(STR_BT_LOW_MEMORY);
  } else if (note != bleturner::LinkNote::None) {
    title = note == bleturner::LinkNote::Connecting ? tr(STR_BT_CONNECTING) : tr(STR_BT_FAILED);
  } else if (automaticPageTurnActive) {
    title = tr(STR_AUTO_TURN_ENABLED) + std::to_string(60 * 1000 / pageTurnDuration);
    const uint8_t statusBarHeight = UITheme::getInstance().getStatusBarHeight();
    if (statusBarHeight == 0 || statusBarHeight == UITheme::getInstance().getProgressBarHeight()) {
      textYOffset += UITheme::getInstance().getMetrics().statusBarVerticalMargin;
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = tr(STR_UNNAMED);
    if (epub) {
      const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
      if (tocIndex != -1) {
        const auto tocItem = epub->getTocItem(tocIndex);
        title = tocItem.title;
      }
    }
  } else if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE) {
    title = epub ? epub->getTitle() : "";
  }

  GUI.drawStatusBar(renderer, bookProgress, currentPage, pageCount, title, 0, textYOffset, true, currentPageBookmarked,
                    section ? section->isBuilding() : false);
}

// ---------------------------------------------------------------------------
// Toolbar reader menu
// ---------------------------------------------------------------------------

namespace {
constexpr StrId kTextRowNames[] = {StrId::STR_FONT, StrId::STR_FONT_SIZE, StrId::STR_LINE_SPACING,
                                   StrId::STR_PARA_ALIGNMENT, StrId::STR_FOCUS_READING};
constexpr StrId kSpacingIds[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId kAlignIds[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr int kTextRowCount = static_cast<int>(std::size(kTextRowNames));
static_assert(std::size(kSpacingIds) == CrossPointSettings::LINE_COMPRESSION_COUNT, "line spacing labels");
static_assert(std::size(kAlignIds) == CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, "alignment labels");
}  // namespace

bool EpubReaderActivity::usesToolbarMenu() const {
  // Both board classes drive the same chrome: touch through the FreeInkUI tap
  // targets, buttons through the focused-tool pill and the panel cursor.
  return SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TOOLBAR;
}

std::string EpubReaderActivity::currentChapterTitle() const {
  if (!epub) return "";
  const int tocIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (tocIndex != -1) {
    return epub->getTocItem(tocIndex).title;
  }
  return tr(STR_UNNAMED);
}

std::string EpubReaderActivity::textRowName(int row) const {
  return row >= 0 && row < kTextRowCount ? I18N.get(kTextRowNames[row]) : "";
}

std::string EpubReaderActivity::textRowValue(int row) const {
  static constexpr StrId kFamily[] = {StrId::STR_NOTO_SANS};
  switch (row) {
    case 0:
      if (SETTINGS.sdFontFamilyName[0] != '\0') return SETTINGS.sdFontFamilyName;
      return I18N.get(kFamily[SETTINGS.fontFamily % CrossPointSettings::FONT_FAMILY_COUNT]);
    case 1:
      return std::to_string(SETTINGS.fontPointSize) + " pt";
    case 2:
      return I18N.get(kSpacingIds[SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT]);
    case 3:
      return I18N.get(kAlignIds[SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT]);
    default:
      return "";
  }
}

// Live apply: persist, re-paginate, and let renderBook() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() {
  applyReaderTextSettings();
  discardOverlayPage();  // the stored page is laid out with the old settings
  requestUpdate();
}

// Settings-style option pickers for the Text panel's enum rows. Every
// selection applies immediately to the page under the sheet.
void EpubReaderActivity::showTextRowPopup(const int row) {
  switch (row) {
    case 1: {
      // The point sizes the active family actually ships.
      const auto sizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      if (sizes.empty()) return;
      std::vector<std::string> labels;
      labels.reserve(sizes.size());
      for (const uint8_t size : sizes) labels.push_back(std::to_string(size) + " pt");
      const uint8_t cur = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
      int curIdx = 0;
      for (size_t i = 0; i < sizes.size(); ++i) {
        if (sizes[i] == cur) curIdx = static_cast<int>(i);
      }
      overlayPopup.show(StrId::STR_FONT_SIZE, labels, curIdx, [this, sizes](int idx) {
        if (idx < 0 || idx >= static_cast<int>(sizes.size())) return;
        SETTINGS.fontPointSize = sizes[idx];
        applyTextSettingLive();
      });
      break;
    }
    case 2:
      overlayPopup.show(StrId::STR_LINE_SPACING, kSpacingIds, static_cast<int>(std::size(kSpacingIds)),
                        SETTINGS.lineSpacing % CrossPointSettings::LINE_COMPRESSION_COUNT, [this](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    case 3:
      overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)),
                        SETTINGS.paragraphAlignment % CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT, [this](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          applyTextSettingLive();
                        });
      break;
    default:
      return;
  }
  paintOverlayPopup();
}

void EpubReaderActivity::discardOverlayPage() {
  if (!overlayPageStored) return;
  renderer.discardStoredBwBuffer();
  overlayPageStored = false;
}

// Push freshly painted overlay chrome. Where the panel supports it the refresh
// is fired deferred: the loop keeps polling input while the waveform runs, so
// the chrome answers taps and buttons the moment it is visible instead of only
// after a blocking displayBuffer() returns. Caller must hold the RenderLock.
void EpubReaderActivity::pushOverlayRefresh() {
  if (renderer.supportsAsyncRefresh()) {
    renderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
    overlayRefreshPending = true;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

// Wait out a pending deferred overlay refresh and reseed the panel's
// differential baseline from the framebuffer: the shadow-free async path skips
// the post-refresh resync, so without this the next FAST diff would run
// against the frame from before the chrome and leave stale pixels on the
// glass. Caller must hold the RenderLock.
void EpubReaderActivity::settleOverlayRefresh() {
  if (!overlayRefreshPending) return;
  overlayRefreshPending = false;
  renderer.cleanupGrayscaleWithFrameBuffer();  // waits, then reseeds the baseline
}

void EpubReaderActivity::openOverlay(Overlay target) {
  runOverlayPolicy(XP_UI_READER_OVERLAY_OPERATION_OPEN_NONE + static_cast<uint8_t>(target));
}

// Grayscale panels re-render to restore AA; other boards restore the B/W snapshot.
void EpubReaderActivity::closeOverlayToPage() { runOverlayPolicy(XP_UI_READER_OVERLAY_OPERATION_CLOSE); }

void EpubReaderActivity::renderOverlay() {
  struct Context {
    EpubReaderActivity& owner;
    std::string chapterTitle;
    std::string pageInfo;
  } context{*this, {}, {}};
  const XpUiReaderOverlayViewServices services{
      &context,
      [](void* ctx, uint8_t query, uint32_t argument) -> uint32_t {
        auto& c = *static_cast<Context*>(ctx);
        auto& o = c.owner;
        switch (static_cast<XpUiReaderOverlayViewQuery>(query)) {
          case XP_UI_READER_VIEW_HAS_BOOK:
            return !!o.epub;
          case XP_UI_READER_VIEW_HAS_SECTION:
            return !!o.section;
          case XP_UI_READER_VIEW_HAS_TOOLBAR:
            return !!o.toolbarUi;
          case XP_UI_READER_VIEW_MODE:
            return static_cast<uint32_t>(o.overlay);
          case XP_UI_READER_VIEW_CURSOR:
            return o.panelCursorShown;
          case XP_UI_READER_VIEW_FOCUS:
            return o.focusedTool;
          case XP_UI_READER_VIEW_CHAPTER_TITLE:
            c.chapterTitle = o.currentChapterTitle();
            return 0;
          case XP_UI_READER_VIEW_PAGE_COUNT:
            return o.section->estimatedTotalPages();
          case XP_UI_READER_VIEW_CURRENT_PAGE:
            return o.section->currentPage;
          case XP_UI_READER_VIEW_PROGRESS: {
            float chapterProgress;
            memcpy(&chapterProgress, &argument, sizeof(chapterProgress));
            const float progress = o.epub->calculateProgress(o.currentSpineIndex, chapterProgress);
            uint32_t bits;
            memcpy(&bits, &progress, sizeof(bits));
            return bits;
          }
          case XP_UI_READER_VIEW_TOUCH:
            return o.mappedInput.hasTouch();
          case XP_UI_READER_VIEW_INDEX:
            return o.panelIndex;
          case XP_UI_READER_VIEW_TOC_COUNT:
            return o.epub->getTocItemsCount();
          case XP_UI_READER_VIEW_MORE_COUNT:
            return o.moreItems.size();
        }
        return 0;
      },
      [](void* ctx, const XpUiReaderOverlayView* view) {
        auto& c = *static_cast<Context*>(ctx);
        auto& o = c.owner;
        ReaderToolbarUi::Model model;
        model.activeTool = view->active_tool;
        model.panel = view->kind != XP_UI_READER_OVERLAY_MODE_TOOLBAR;
        model.denseRows = view->dense;
        model.selectedIndex = view->selected_index;
        model.itemCount = view->item_count;
        model.progressPermille = view->progress_permille;
        switch (static_cast<XpUiReaderOverlayMode>(view->kind)) {
          case XP_UI_READER_OVERLAY_MODE_TOOLBAR:
            model.chapterTitle = c.chapterTitle.c_str();
            c.pageInfo = reinterpret_cast<const char*>(view->page_info);
            model.pageInfo = c.pageInfo.c_str();
            break;
          case XP_UI_READER_OVERLAY_MODE_CONTENTS:
            model.panelTitle = tr(STR_TOOL_CONTENTS);
            model.rowText = [&o](int i) {
              const auto item = o.epub->getTocItem(i);
              const int depth = item.level > 1 ? (item.level - 1) * 2 : 0;
              return std::string(depth, ' ') + item.title;
            };
            break;
          case XP_UI_READER_OVERLAY_MODE_TEXT:
            model.panelTitle = tr(STR_TOOL_TEXT);
            model.rowText = [&o](int i) { return o.textRowName(i); };
            model.rowValue = [&o](int i) { return o.textRowValue(i); };
            model.rowCheckbox = [](void*, int i) -> ReaderToolbarUi::RowFlags {
              const bool toggle = xp_ui_reader_overlay_checkbox(XP_UI_READER_OVERLAY_MODE_TEXT, i) != 0;
              return {toggle, toggle && SETTINGS.focusReadingEnabled};
            };
            break;
          case XP_UI_READER_OVERLAY_MODE_MORE:
            model.panelTitle = tr(STR_TOOL_MORE);
            model.rowText = [&o](int i) { return o.moreRowName(i); };
            model.rowValue = [&o](int i) { return o.moreRowValue(i); };
            model.rowCheckboxContext = &o;
            model.rowCheckbox = [](void* ctx, int i) -> ReaderToolbarUi::RowFlags {
              const auto* self = static_cast<EpubReaderActivity*>(ctx);
              using MA = EpubReaderMenuActivity::MenuAction;
              const auto action = self->moreItems[i].action;
              static_assert(static_cast<int>(MA::NIGHT_MODE) == 3);
              static_assert(static_cast<int>(MA::FRONTLIGHT) == 4);
              static_assert(static_cast<int>(MA::BLUETOOTH) == 5);
              switch (static_cast<XpUiReaderDecoration>(
                  xp_ui_reader_overlay_checkbox(XP_UI_READER_OVERLAY_MODE_MORE, static_cast<int32_t>(action)))) {
                case XP_UI_READER_DECORATION_NIGHT:
                  return {true, SETTINGS.screenInverted != 0};
                case XP_UI_READER_DECORATION_BLUETOOTH:
                  return {true, SETTINGS.ble.enabled != 0};
                case XP_UI_READER_DECORATION_FRONTLIGHT:
                  return {true, Frontlight.isOn()};
                case XP_UI_READER_DECORATION_NONE:
                case XP_UI_READER_DECORATION_FOCUS:
                  return {};
              }
              return {};
            };
            break;
          case XP_UI_READER_OVERLAY_MODE_NONE:
            break;
        }
        o.toolbarUi->setModel(model);
        o.toolbarUi->render();
      }};
  xp_ui_reader_overlay_view(&services);
}

void EpubReaderActivity::handleOverlayInput() { runOverlayPolicy(XP_UI_READER_OVERLAY_OPERATION_INPUT); }
void EpubReaderActivity::runOverlayPolicy(uint8_t operation) {
  struct Context {
    EpubReaderActivity& o;
    std::optional<RenderLock> lock{};
    std::optional<decltype(std::declval<Epub&>().getTocItem(0))> item{};
    ReaderToolbarUi::Routed routed{};
    uint32_t pressed = 0, released = 0;
    uint32_t read(uint8_t field) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderOverlayField>(field)) {
        case XP_UI_READER_OVERLAY_FIELD_OVERLAY:
          return static_cast<uint32_t>(o.overlay);
        case XP_UI_READER_OVERLAY_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_OVERLAY_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_OVERLAY_FIELD_FOCUS:
          return static_cast<uint32_t>(o.focusedTool);
        case XP_UI_READER_OVERLAY_FIELD_INDEX:
          return static_cast<uint32_t>(o.panelIndex);
        case XP_UI_READER_OVERLAY_FIELD_CURSOR:
          return static_cast<uint32_t>(o.panelCursorShown);
        case XP_UI_READER_OVERLAY_FIELD_HOLD:
          return static_cast<uint32_t>(o.panelHoldJumped);
        case XP_UI_READER_OVERLAY_FIELD_STORED:
          return static_cast<uint32_t>(o.overlayPageStored);
      }
      return 0;
    }
    void write(uint8_t field, uint32_t value) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderOverlayField>(field)) {
        case XP_UI_READER_OVERLAY_FIELD_OVERLAY:
          o.overlay = static_cast<decltype(o.overlay)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_SPINE:
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_NEXT_PAGE:
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_FOCUS:
          o.focusedTool = static_cast<decltype(o.focusedTool)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_INDEX:
          o.panelIndex = static_cast<decltype(o.panelIndex)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_CURSOR:
          o.panelCursorShown = static_cast<decltype(o.panelCursorShown)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_HOLD:
          o.panelHoldJumped = static_cast<decltype(o.panelHoldJumped)>(value);
          break;
        case XP_UI_READER_OVERLAY_FIELD_STORED:
          o.overlayPageStored = static_cast<decltype(o.overlayPageStored)>(value);
          break;
      }
    }
    uint32_t query(uint8_t query) {
      switch (static_cast<XpUiReaderOverlayQuery>(query)) {
        case XP_UI_READER_OVERLAY_QUERY_HAS_SECTION:
          return bool(o.section);
        case XP_UI_READER_OVERLAY_QUERY_PAGE_STALE:
          return o.pageBufferStale;
        case XP_UI_READER_OVERLAY_QUERY_TOUCH:
          return o.mappedInput.hasTouch();
        case XP_UI_READER_OVERLAY_QUERY_CURRENT_TOC:
          return o.currentTocIndex();
        case XP_UI_READER_OVERLAY_QUERY_IS_A4:
          return BoardConfig::isEegoA4();
        case XP_UI_READER_OVERLAY_QUERY_IS_XTEINK:
          return xteinkClassPanel();
        case XP_UI_READER_OVERLAY_QUERY_HAS_TOOLBAR:
          return bool(o.toolbarUi);
        case XP_UI_READER_OVERLAY_QUERY_POPUP_ACTIVE:
          return o.overlayPopup.isActive();
        case XP_UI_READER_OVERLAY_QUERY_SPINE_COUNT:
          return o.epub->getSpineItemsCount();
        case XP_UI_READER_OVERLAY_QUERY_TOC_COUNT:
          return o.epub->getTocItemsCount();
        case XP_UI_READER_OVERLAY_QUERY_MORE_COUNT:
          return o.moreItems.size();
        case XP_UI_READER_OVERLAY_QUERY_ROWS:
          return o.toolbarUi->visibleRows();
        case XP_UI_READER_OVERLAY_QUERY_ROUTED:
          return routed.routed;
        case XP_UI_READER_OVERLAY_QUERY_VALUE:
          return routed.value;
        case XP_UI_READER_OVERLAY_QUERY_PERMILLE:
          return routed.permille;
        case XP_UI_READER_OVERLAY_QUERY_X:
          return routed.x;
        case XP_UI_READER_OVERLAY_QUERY_Y:
          return routed.y;
        case XP_UI_READER_OVERLAY_QUERY_WIDTH:
          return o.renderer.getScreenWidth();
        case XP_UI_READER_OVERLAY_QUERY_HEIGHT:
          return o.renderer.getScreenHeight();
        case XP_UI_READER_OVERLAY_QUERY_FOCUS_READING:
          return SETTINGS.focusReadingEnabled;
        case XP_UI_READER_OVERLAY_QUERY_PRESSED:
          return pressed;
        case XP_UI_READER_OVERLAY_QUERY_RELEASED:
          return released;
      }
      return 0;
    }
    uint32_t command(uint8_t cmd, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderOverlayCommand>(cmd)) {
        case XP_UI_READER_OVERLAY_COMMAND_RESET_HOME:
          o.mappedInput.resetHomeButtonInput();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_CREATE_TOOLBAR:
          o.toolbarUi = std::make_unique<ReaderToolbarUi>(o.renderer);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_RESET_NAVIGATION:
          o.toolbarUi->resetNavigation(a, b);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_BUILD_MORE:
          o.buildMoreActions();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_DROP_TOOLBAR:
          o.toolbarUi.reset();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_FORCE_COUNTER:
          o.setRefreshCounter(a);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_DISPLAY_FAST:
          o.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_TAKE_SWIPE:
          return static_cast<uint32_t>(o.mappedInput.wasSwipe());
        case XP_UI_READER_OVERLAY_COMMAND_TAKE_HELD_MS:
          return o.mappedInput.getHeldTime();
        case XP_UI_READER_OVERLAY_COMMAND_SAMPLE_BUTTONS: {
          const auto sample = [&](uint32_t bit, MappedInputManager::Button button) {
            if (static_cast<uint32_t>(a) & bit) {
              pressed &= ~bit;
              if (o.mappedInput.isPressed(button)) pressed |= bit;
            }
            if (static_cast<uint32_t>(b) & bit) {
              released &= ~bit;
              if (o.mappedInput.wasReleased(button)) released |= bit;
            }
          };
          sample(XP_UI_BACK, MappedInputManager::Button::Back);
          sample(XP_UI_LEFT, MappedInputManager::Button::Left);
          sample(XP_UI_RIGHT, MappedInputManager::Button::Right);
          sample(XP_UI_CONFIRM, MappedInputManager::Button::Confirm);
          sample(XP_UI_UP, MappedInputManager::Button::Up);
          sample(XP_UI_DOWN, MappedInputManager::Button::Down);

          break;
        }
        case XP_UI_READER_OVERLAY_COMMAND_POPUP_INPUT:
          o.overlayPopup.handleInput(
              o.mappedInput, [owner = &o] { owner->runOverlayPolicy(XP_UI_READER_OVERLAY_OPERATION_POPUP_CHANGED); });
          break;
        case XP_UI_READER_OVERLAY_COMMAND_LOCK:
          assert(!lock);
          lock.emplace();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_UNLOCK:
          assert(lock);
          lock.reset();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_SETTLE:
          o.settleOverlayRefresh();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_PAINT_POPUP:
          o.paintOverlayPopup();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_RESTORE:
          o.renderer.restoreBwBuffer(false);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_STORE:
          return o.renderer.storeBwBuffer();
        case XP_UI_READER_OVERLAY_COMMAND_RENDER:
          o.renderOverlay();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_PUSH:
          o.pushOverlayRefresh();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_UPDATE:
          o.requestUpdate();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_CLOSE:
          o.closeOverlayToPage();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_OPEN:
          o.openOverlay(static_cast<Overlay>(a));
          break;
        case XP_UI_READER_OVERLAY_COMMAND_CLEAR_DEFERRED:
          o.clearDeferredReposition();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_DROP_SECTION:
          o.section.reset();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_ROUTE:
          routed = o.toolbarUi->route(o.mappedInput);
          return static_cast<uint32_t>(routed.event);
        case XP_UI_READER_OVERLAY_COMMAND_DISMISS_POPUP:
          o.overlayPopup.dismiss();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_DISCARD:
          o.discardOverlayPage();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_START_PICKER:
          o.startActivityForResult(
              std::make_unique<TextSettingsActivity>(o.renderer, o.mappedInput, &sdFontSystem.registry(),
                                                     TextSettingsActivity::Tab::Family),
              [owner = &o](const ActivityResult&) {
                owner->runOverlayPolicy(XP_UI_READER_OVERLAY_OPERATION_PICKER_RESULT);
              });
          break;
        case XP_UI_READER_OVERLAY_COMMAND_APPLY_TEXT:
          o.applyReaderTextSettings();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_TOOLBAR_BEGIN:
          o.toolbarUi->begin();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_SET_FOCUS_READING:
          SETTINGS.focusReadingEnabled = a;
          break;
        case XP_UI_READER_OVERLAY_COMMAND_APPLY_LIVE:
          o.applyTextSettingLive();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_TEXT_POPUP:
          o.showTextRowPopup(a);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_MORE:
          o.activateMoreRow(a);
          break;
        case XP_UI_READER_OVERLAY_COMMAND_TOC_ITEM:
          item.emplace(o.epub->getTocItem(a));
          return item->spineIndex;
        case XP_UI_READER_OVERLAY_COMMAND_TOC_ANCHOR:
          o.pendingAnchor = item->anchor;
          break;
        case XP_UI_READER_OVERLAY_COMMAND_TOC_DROP:
          item.reset();
          break;
        case XP_UI_READER_OVERLAY_COMMAND_SCROLL:
          return o.toolbarUi->scrollNavigation(a, b);
      }
      return 0;
    }
  } context{*this};
  static_assert(static_cast<int>(Overlay::None) == XP_UI_READER_OVERLAY_MODE_NONE);
  static_assert(static_cast<int>(Overlay::Toolbar) == XP_UI_READER_OVERLAY_MODE_TOOLBAR);
  static_assert(static_cast<int>(Overlay::Contents) == XP_UI_READER_OVERLAY_MODE_CONTENTS);
  static_assert(static_cast<int>(Overlay::Text) == XP_UI_READER_OVERLAY_MODE_TEXT);
  static_assert(static_cast<int>(Overlay::More) == XP_UI_READER_OVERLAY_MODE_MORE);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::None) == XP_UI_READER_OVERLAY_EVENT_NONE);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::Dismiss) == XP_UI_READER_OVERLAY_EVENT_DISMISS);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::Tool) == XP_UI_READER_OVERLAY_EVENT_TOOL);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::PrevChapter) == XP_UI_READER_OVERLAY_EVENT_PREV_CHAPTER);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::NextChapter) == XP_UI_READER_OVERLAY_EVENT_NEXT_CHAPTER);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::Scrub) == XP_UI_READER_OVERLAY_EVENT_SCRUB);
  static_assert(static_cast<int>(ReaderToolbarUi::Event::Row) == XP_UI_READER_OVERLAY_EVENT_ROW);
  static_assert(static_cast<int>(MappedInputManager::SwipeDir::None) == XP_UI_READER_OVERLAY_SWIPE_NONE);
  static_assert(static_cast<int>(MappedInputManager::SwipeDir::Left) == XP_UI_READER_OVERLAY_SWIPE_LEFT);
  static_assert(static_cast<int>(MappedInputManager::SwipeDir::Right) == XP_UI_READER_OVERLAY_SWIPE_RIGHT);
  static_assert(static_cast<int>(MappedInputManager::SwipeDir::Up) == XP_UI_READER_OVERLAY_SWIPE_UP);
  static_assert(static_cast<int>(MappedInputManager::SwipeDir::Down) == XP_UI_READER_OVERLAY_SWIPE_DOWN);
  static_assert(kTextRowCount == XP_UI_READER_OVERLAY_TEXT_ROWS);
  static_assert(PANEL_HOLD_MS == XP_UI_READER_OVERLAY_HOLD_MS);
  static_assert(PANEL_HOLD_STEP == XP_UI_READER_OVERLAY_HOLD_ROWS);

  const XpUiReaderOverlayServices services{
      &context, [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t q) { return static_cast<Context*>(p)->query(q); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); }};
  xp_ui_reader_overlay_run(&services, operation);
  assert(!context.lock);
  assert(!context.item);
}

// First paint of the option picker over the panel (and highlight repaints).
// The dialog draws over the current framebuffer without clearing; erasing it
// on dismissal is the popup gate's restore in handleOverlayInput().
void EpubReaderActivity::paintOverlayPopup() {
  RenderLock lock;
  settleOverlayRefresh();
  overlayPopup.render(renderer);
  pushOverlayRefresh();
}

void EpubReaderActivity::applyReaderTextSettings() {
  {
    RenderLock lock;
    if (section) {
      rememberCurrentContentOffset();
      cachedSpineIndex = currentSpineIndex;
      cachedChapterTotalPageCount = section->pageCount;
      nextPageNumber = section->currentPage;
    }
    // Release the old parser and page table before settings serialization needs heap.
    section.reset();  // force re-pagination with the new settings
  }
  SETTINGS.saveToFile();
  // (Re)load or unload the selected SD-card font for the current family/size.
  // The reader otherwise only loads SD fonts on book open, so without this an
  // in-reader font change wouldn't take effect until re-opening the book.
  // Locked because the render task can load or unload SD font variants too.
  RenderLock lock;
  sdFontSystem.ensureLoaded(renderer);
}

// The More panel carries everything the classic list menu offers except the
// two entries that have their own tool (chapters -> Contents, text -> Text).
void EpubReaderActivity::buildMoreActions() {
  using MA = EpubReaderMenuActivity::MenuAction;
  bool hasFootnotes;
  {
    RenderLock lock;
    hasFootnotes = !currentPageMetadata.footnotes().empty();
  }
  EpubReaderMenuActivity::buildMenuItems(moreItems, hasFootnotes, !cachedBookmarks.empty(), CLIPPINGS.hasClippings());
  moreItems.erase(std::remove_if(moreItems.begin(), moreItems.end(),
                                 [](const auto& item) {
                                   return item.action == MA::SELECT_CHAPTER || item.action == MA::TEXT_SETTINGS;
                                 }),
                  moreItems.end());
}

std::string EpubReaderActivity::moreRowName(int row) const {
  return row >= 0 && row < static_cast<int>(moreItems.size()) ? I18N.get(moreItems[row].labelId) : "";
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  using MA = EpubReaderMenuActivity::MenuAction;
  static constexpr StrId kOrient[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                      StrId::STR_LANDSCAPE_CCW};
  static_assert(std::size(kOrient) == CrossPointSettings::ORIENTATION_COUNT, "orientation labels");
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return "";
  switch (moreItems[row].action) {
    case MA::ROTATE_SCREEN:
      return I18N.get(kOrient[SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT]);
    case MA::AUTO_PAGE_TURN:
      return (autoTurnOption == 0 || autoTurnOption >= static_cast<int>(std::size(PAGE_TURN_RATES)))
                 ? std::string(tr(STR_STATE_OFF))
                 : std::to_string(PAGE_TURN_RATES[autoTurnOption]);
    default:
      return "";
  }
}

void EpubReaderActivity::activateMoreRow(int row) {
  using MA = EpubReaderMenuActivity::MenuAction;
  if (row < 0 || row >= static_cast<int>(moreItems.size())) return;
  {
    RenderLock lock;  // several actions launch screens that paint the framebuffer
    settleOverlayRefresh();
  }
  const auto action = moreItems[row].action;
  // In-place toggles keep the panel open and re-render the page beneath it.
  switch (action) {
    case MA::ROTATE_SCREEN: {
      static constexpr StrId kOrientIds[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
      static_assert(std::size(kOrientIds) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
      overlayPopup.show(StrId::STR_ORIENTATION, kOrientIds, static_cast<int>(std::size(kOrientIds)),
                        SETTINGS.orientation % CrossPointSettings::ORIENTATION_COUNT, [this](int idx) {
                          if (idx == SETTINGS.orientation) return;
                          applyOrientation(static_cast<uint8_t>(idx));
                          // The stored page is laid out for the old orientation.
                          discardOverlayPage();
                          requestUpdate();
                        });
      paintOverlayPopup();
      return;
    }
    case MA::AUTO_PAGE_TURN: {
      std::vector<std::string> labels;
      labels.reserve(std::size(PAGE_TURN_RATES));
      labels.emplace_back(tr(STR_STATE_OFF));
      for (size_t i = 1; i < std::size(PAGE_TURN_RATES); ++i) labels.push_back(std::to_string(PAGE_TURN_RATES[i]));
      overlayPopup.show(StrId::STR_AUTO_TURN_PAGES_PER_MIN, labels, autoTurnOption, [this](int idx) {
        autoTurnOption = idx;
        toggleAutoPageTurn(static_cast<uint8_t>(idx));
      });
      paintOverlayPopup();
      return;
    }
    case MA::BLUETOOTH:
      EpubReaderMenuActivity::toggleBluetooth();
      {
        RenderLock lock;
        renderOverlay();
        pushOverlayRefresh();
      }
      return;
    case MA::NIGHT_MODE:
      SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      discardOverlayPage();
      requestUpdate();
      return;
    case MA::FRONTLIGHT: {
      const bool lightOn = !Frontlight.isOn();
      Frontlight.setOn(lightOn);
      SETTINGS.frontlightOn = lightOn ? 1 : 0;
      SETTINGS.saveToFile();
      {
        RenderLock lock;  // the render task shares the framebuffer
        renderOverlay();
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }
      return;
    }
    default:
      break;
  }
  // Leaf actions open their own screen / perform the action; close the overlay first.
  overlay = Overlay::None;
  if (action == MA::GO_TO_PERCENT && overlayPageStored) {
    // The percent dialog is a popup over the current frame: wipe the toolbar
    // chrome back to the clean page first so the dialog draws over the page,
    // not the sheet. No refresh push — the dialog's first frame carries it.
    RenderLock lock;
    settleOverlayRefresh();
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    overlayPageStored = false;
  } else {
    discardOverlayPage();
  }
  if (action == MA::TOGGLE_BOOKMARK) {
    // No child activity here to trigger the re-render the list menu relies on:
    // show the same confirmation popup the long-press path does.
    addBookmark();
    showBookmarkMessage = true;
    bookmarkMessageTime = millis();
    requestUpdate();
    return;
  }
  onReaderMenuConfirm(action);
  // Child screens re-render the reader on Pop; GO_HOME/DELETE_CACHE leave the
  // reader entirely. In-place actions would otherwise leave the closed panel
  // on screen.
  if (action != MA::GO_HOME && action != MA::DELETE_CACHE) requestUpdate();
}

void EpubReaderActivity::navigateToHref(const std::string& href, bool savePosition) {
  runNavigationPolicy(XP_UI_READER_NAV_OPERATION_HREF, savePosition, 0, 0, &href);
}

void EpubReaderActivity::saveLinkStack() const {
  uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
  XpUiReaderLinkPosition positions[MAX_FOOTNOTE_DEPTH];
  for (int i = 0; i < MAX_FOOTNOTE_DEPTH; ++i)
    positions[i] = {savedPositions[i].spineIndex, savedPositions[i].pageNumber};
  const size_t size = xp_ui_reader_links_encode(positions, static_cast<uint8_t>(footnoteDepth), data);
  HalFile f;
  if (!Storage.openFileForWrite("ERS", epub->getCachePath() + "/links.bin", f)) return;
  if (f.write(data, size) != size) LOG_ERR("ERS", "Failed to write link stack");
}

void EpubReaderActivity::loadLinkStack() {
  const std::string path = epub->getCachePath() + "/links.bin";
  if (!Storage.exists(path.c_str())) return;
  {
    HalFile f;
    uint8_t data[1 + MAX_FOOTNOTE_DEPTH * 4];
    if (Storage.openFileForRead("ERS", path, f)) {
      const int size = f.read(data, sizeof(data));
      XpUiReaderLinkPosition positions[MAX_FOOTNOTE_DEPTH];
      for (int i = 0; i < MAX_FOOTNOTE_DEPTH; ++i)
        positions[i] = {savedPositions[i].spineIndex, savedPositions[i].pageNumber};
      const int depth =
          xp_ui_reader_links_decode(data, static_cast<uint32_t>(size), epub->getSpineItemsCount(), positions);
      for (int i = 0; i < MAX_FOOTNOTE_DEPTH; ++i) savedPositions[i] = {positions[i].spine, positions[i].page};
      if (depth != 0) {
        footnoteDepth = depth;
        LOG_DBG("ERS", "Loaded link stack, depth %d", depth);
      }
    }
  }
  Storage.remove(path.c_str());
}

void EpubReaderActivity::restoreSavedPosition() { runNavigationPolicy(XP_UI_READER_NAV_OPERATION_RESTORE); }

void EpubReaderActivity::loadCachedBookmarks() {
  cachedBookmarks.clear();
  if (!epub) {
    currentPageBookmarked = false;
    return;
  }

  BookmarkFile::load(epub->getPath(), cachedBookmarks);
  updateBookmarkFlag();
}

void EpubReaderActivity::addBookmark() {
  if (!section || !epub) return;
  LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", currentSpineIndex, section ? section->currentPage : -1);
  int currentPage;
  int pageCount;
  {
    RenderLock lock;
    pageCount = section->estimatedTotalPages();
    currentPage = section->currentPage;
  }

  SavedProgressPosition progress = ProgressMapper::toSavedProgress(epub, getCurrentPosition());
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, currentPage, pageCount);

  const size_t bookmarkCountBeforeToggle = cachedBookmarks.size();
  cachedBookmarks.erase(std::remove_if(cachedBookmarks.begin(), cachedBookmarks.end(),
                                       [&](const BookmarkEntry& b) {
                                         return bookmarkMatchesProgress(b, currentSpineIndex, currentPage, pageCount,
                                                                        pageRange);
                                       }),
                        cachedBookmarks.end());
  if (cachedBookmarks.size() != bookmarkCountBeforeToggle) {
    bookmarkRemoved = true;
    currentPageBookmarked = false;
  } else {
    std::string pageText;
    if (currentPage >= 0 && currentPage < pageCount) {
      pageText = section->getTextFromSectionFile();
    }
    BookmarkEntry entry;
    entry.percentage = progress.percentage;
    entry.xpath = progress.xpath;
    entry.summary = BookmarkUtil::sanitizeBookmarkSummary(pageText);
    entry.computedSpineIndex = currentSpineIndex;
    entry.computedChapterPageCount = pageCount;
    entry.computedChapterProgress = currentPage;
    const std::optional<uint32_t> offset =
        currentPageVisibleOffset.has_value() ? currentPageVisibleOffset
        : (currentPage >= 0 && currentPage < section->pageCount)
            ? section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))
            : std::nullopt;
    if (offset.has_value()) {
      entry.visibleTextOffset = *offset;
      entry.hasVisibleTextOffset = true;
    }
    cachedBookmarks.insert(cachedBookmarks.begin(), entry);
    bookmarkRemoved = false;
    currentPageBookmarked = true;
  }

  if (!BookmarkFile::save(epub->getPath(), cachedBookmarks)) {
    LOG_ERR("ERS", "Failed to save bookmarks");
  }
  requestUpdate();
}

void EpubReaderActivity::updateBookmarkFlag() {
  if (!section || !epub || cachedBookmarks.empty()) {
    currentPageBookmarked = false;
    return;
  }
  const int pageCount = section->estimatedTotalPages();
  const ProgressRange pageRange = getPageProgressRange(epub, currentSpineIndex, section->currentPage, pageCount);
  currentPageBookmarked = std::any_of(cachedBookmarks.begin(), cachedBookmarks.end(), [&](const BookmarkEntry& b) {
    return bookmarkMatchesProgress(b, currentSpineIndex, section->currentPage, pageCount, pageRange);
  });
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Epub;
  if (epub) {
    snprintf(info.title, sizeof(info.title), "%s", epub->getTitle().c_str());
    info.spineIndex = currentSpineIndex;
  }
  if (section) {
    info.currentPage = section->currentPage + 1;
    info.totalPages = section->estimatedTotalPages();
    if (epub && epub->getBookSize() > 0 && info.totalPages > 0) {
      const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(info.totalPages);
      int pct = static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 100.0f + 0.5f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      info.progressPercent = pct;
    }
  }
  return info;
}

int EpubReaderActivity::getProgressBasisPoints() const {
  if (isAtEndOfBook()) return 10000;
  if (!epub || !section || epub->getBookSize() == 0) return getProgressPercent() * 100;
  const int totalPages = section->estimatedTotalPages();
  if (totalPages <= 0) return getProgressPercent() * 100;
  const float chapterProgress = static_cast<float>(section->currentPage) / static_cast<float>(totalPages);
  const int basisPoints =
      static_cast<int>(epub->calculateProgress(currentSpineIndex, chapterProgress) * 10000.0f + 0.5f);
  return std::clamp(basisPoints, 0, 10000);
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  const int currentPage = section ? section->currentPage : nextPageNumber;
  const int totalPages = section ? section->estimatedTotalPages() : cachedChapterTotalPageCount;
  std::optional<uint16_t> paragraphIndex;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    const uint16_t paragraphPage =
        currentPage > 0 ? static_cast<uint16_t>(currentPage - 1) : static_cast<uint16_t>(currentPage);
    if (const auto pIdx = section->getParagraphIndexForPage(paragraphPage)) {
      paragraphIndex = *pIdx;
    }
  }

  CrossPointPosition localPos = {currentSpineIndex, currentPage, totalPages};
  localPos.hasResolvedSpineIndex = true;
  localPos.hasMappedPage = true;
  if (section && currentPage >= 0 && currentPage < section->pageCount) {
    if (const auto offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(currentPage))) {
      localPos.visibleTextOffset = *offset;
      localPos.hasVisibleTextOffset = true;
    }
  }
  if (paragraphIndex.has_value()) {
    localPos.paragraphIndex = *paragraphIndex;
    localPos.hasParagraphIndex = true;
  }
  return localPos;
}

bool EpubReaderActivity::runNavigationPolicy(uint8_t operation, int32_t a, int32_t b, int32_t c,
                                             const std::string* href, const ProgressChangeResult* sync) {
  struct Context {
    EpubReaderActivity& o;
    int32_t spineArgument, pageArgument, countArgument;
    const std::string* href;
    const ProgressChangeResult* sync;
    std::optional<RenderLock> lock;
    std::optional<uint32_t> offset;
    CrossPointPosition fallback{};
    std::string anchor;
    uint32_t read(uint8_t field) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderNavField>(field)) {
        case XP_UI_READER_NAV_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_NAV_FIELD_CACHED_SPINE:
          return static_cast<uint32_t>(o.cachedSpineIndex);
        case XP_UI_READER_NAV_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_NAV_FIELD_CACHED_COUNT:
          return static_cast<uint32_t>(o.cachedChapterTotalPageCount);
        case XP_UI_READER_NAV_FIELD_HAS_CACHED_OFFSET:
          return static_cast<uint32_t>(o.cachedVisibleTextOffset.has_value());
        case XP_UI_READER_NAV_FIELD_CACHED_OFFSET:
          return static_cast<uint32_t>(o.cachedVisibleTextOffset.value_or(0));
        case XP_UI_READER_NAV_FIELD_HAS_CURRENT_OFFSET:
          return static_cast<uint32_t>(o.currentPageVisibleOffset.has_value());
        case XP_UI_READER_NAV_FIELD_CURRENT_OFFSET:
          return static_cast<uint32_t>(o.currentPageVisibleOffset.value_or(0));
        case XP_UI_READER_NAV_FIELD_PENDING_OFFSET:
          return static_cast<uint32_t>(o.pendingOffsetJump.value_or(0));
        case XP_UI_READER_NAV_FIELD_PERCENT_JUMP:
          return static_cast<uint32_t>(o.pendingPercentJump);
        case XP_UI_READER_NAV_FIELD_SPINE_PROGRESS: {
          uint32_t bits;
          memcpy(&bits, &o.pendingSpineProgress, sizeof(bits));
          return bits;
        }
        case XP_UI_READER_NAV_FIELD_FOOTNOTE_DEPTH:
          return static_cast<uint32_t>(o.footnoteDepth);
        case XP_UI_READER_NAV_FIELD_SAVED_SPINE0:
          return static_cast<uint32_t>(o.savedPositions[0].spineIndex);
        case XP_UI_READER_NAV_FIELD_SAVED_PAGE0:
          return static_cast<uint32_t>(o.savedPositions[0].pageNumber);
        case XP_UI_READER_NAV_FIELD_SAVED_SPINE1:
          return static_cast<uint32_t>(o.savedPositions[1].spineIndex);
        case XP_UI_READER_NAV_FIELD_SAVED_PAGE1:
          return static_cast<uint32_t>(o.savedPositions[1].pageNumber);
        case XP_UI_READER_NAV_FIELD_SAVED_SPINE2:
          return static_cast<uint32_t>(o.savedPositions[2].spineIndex);
        case XP_UI_READER_NAV_FIELD_SAVED_PAGE2:
          return static_cast<uint32_t>(o.savedPositions[2].pageNumber);
      }
      return 0;
    }
    void write(uint8_t field, uint32_t value) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderNavField>(field)) {
        case XP_UI_READER_NAV_FIELD_SPINE:
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_CACHED_SPINE:
          o.cachedSpineIndex = static_cast<decltype(o.cachedSpineIndex)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_NEXT_PAGE:
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_CACHED_COUNT:
          o.cachedChapterTotalPageCount = static_cast<decltype(o.cachedChapterTotalPageCount)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_HAS_CACHED_OFFSET:
          if (!value) o.cachedVisibleTextOffset.reset();
          break;
        case XP_UI_READER_NAV_FIELD_CACHED_OFFSET:
          o.cachedVisibleTextOffset = value;
          break;
        case XP_UI_READER_NAV_FIELD_HAS_CURRENT_OFFSET:
          assert(false);
          break;
        case XP_UI_READER_NAV_FIELD_CURRENT_OFFSET:
          assert(false);
          break;
        case XP_UI_READER_NAV_FIELD_PENDING_OFFSET:
          o.pendingOffsetJump = value;
          break;
        case XP_UI_READER_NAV_FIELD_PERCENT_JUMP:
          o.pendingPercentJump = static_cast<decltype(o.pendingPercentJump)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SPINE_PROGRESS:
          memcpy(&o.pendingSpineProgress, &value, sizeof(value));
          break;
        case XP_UI_READER_NAV_FIELD_FOOTNOTE_DEPTH:
          o.footnoteDepth = static_cast<decltype(o.footnoteDepth)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SAVED_SPINE0:
          o.savedPositions[0].spineIndex = static_cast<decltype(o.savedPositions[0].spineIndex)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SAVED_PAGE0:
          o.savedPositions[0].pageNumber = static_cast<decltype(o.savedPositions[0].pageNumber)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SAVED_SPINE1:
          o.savedPositions[1].spineIndex = static_cast<decltype(o.savedPositions[1].spineIndex)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SAVED_PAGE1:
          o.savedPositions[1].pageNumber = static_cast<decltype(o.savedPositions[1].pageNumber)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SAVED_SPINE2:
          o.savedPositions[2].spineIndex = static_cast<decltype(o.savedPositions[2].spineIndex)>(value);
          break;
        case XP_UI_READER_NAV_FIELD_SAVED_PAGE2:
          o.savedPositions[2].pageNumber = static_cast<decltype(o.savedPositions[2].pageNumber)>(value);
          break;
      }
    }
    uint32_t query(uint8_t query, uint32_t argument) {
      switch (static_cast<XpUiReaderNavQuery>(query)) {
        case XP_UI_READER_NAV_QUERY_HAS_BOOK: {
          return bool(o.epub);
        }
        case XP_UI_READER_NAV_QUERY_BOOK_SIZE: {
          return o.epub->getBookSize();
        }
        case XP_UI_READER_NAV_QUERY_SPINE_COUNT: {
          return o.epub->getSpineItemsCount();
        }
        case XP_UI_READER_NAV_QUERY_FIND_SPINE: {
          return o.epub->getSpineIndexForSize(argument);
        }
        case XP_UI_READER_NAV_QUERY_CUMULATIVE: {
          return o.epub->getCumulativeSpineItemSize(static_cast<int32_t>(argument));
        }
        case XP_UI_READER_NAV_QUERY_HAS_SECTION: {
          return bool(o.section);
        }
        case XP_UI_READER_NAV_QUERY_SECTION_PAGE: {
          return o.section->currentPage;
        }
        case XP_UI_READER_NAV_QUERY_SECTION_COUNT: {
          return o.section->pageCount;
        }
        case XP_UI_READER_NAV_QUERY_ESTIMATED_COUNT: {
          return o.section->estimatedTotalPages();
        }
        case XP_UI_READER_NAV_QUERY_BUILDING: {
          return o.section->isBuilding();
        }
        case XP_UI_READER_NAV_QUERY_MAP_OFFSET: {
          const auto page = o.section->getPageForVisibleTextOffset(argument);
          return page ? 0x10000u | *page : 0;
        }
        case XP_UI_READER_NAV_QUERY_GET_OFFSET: {
          offset = o.section->getVisibleTextOffsetForPage(static_cast<uint16_t>(argument));
          return offset.has_value();
        }
        case XP_UI_READER_NAV_QUERY_OFFSET_VALUE: {
          return offset.value_or(0);
        }
        case XP_UI_READER_NAV_QUERY_FALLBACK_SPINE: {
          return fallback.spineIndex;
        }
        case XP_UI_READER_NAV_QUERY_FALLBACK_PAGE: {
          return fallback.pageNumber;
        }
        case XP_UI_READER_NAV_QUERY_SYNC_HAS_OFFSET: {
          return sync->hasVisibleTextOffset;
        }
        case XP_UI_READER_NAV_QUERY_SYNC_OFFSET: {
          return sync->visibleTextOffset;
        }
        case XP_UI_READER_NAV_QUERY_SYNC_SPINE: {
          return sync->spineIndex;
        }
        case XP_UI_READER_NAV_QUERY_SYNC_PAGE: {
          return sync->page;
        }
        case XP_UI_READER_NAV_QUERY_SYNC_COUNT: {
          return sync->totalPages;
        }
        case XP_UI_READER_NAV_QUERY_SYNC_HAS_PROGRESS: {
          return sync->hasSavedProgress;
        }
        case XP_UI_READER_NAV_QUERY_RESOLVE_HREF: {
          return o.epub->resolveHrefToSpineIndex(*href);
        }
      }
      return 0;
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderNavCommand>(command)) {
        case XP_UI_READER_NAV_COMMAND_LOCK: {
          assert(!lock);
          lock.emplace();
          break;
        }
        case XP_UI_READER_NAV_COMMAND_UNLOCK: {
          assert(lock);
          lock.reset();
          break;
        }
        case XP_UI_READER_NAV_COMMAND_DROP_SECTION: {
          o.section.reset();
          break;
        }
        case XP_UI_READER_NAV_COMMAND_UPDATE: {
          o.requestUpdate();
          break;
        }
        case XP_UI_READER_NAV_COMMAND_SET_SECTION_PAGE: {
          o.section->currentPage = a;
          break;
        }
        case XP_UI_READER_NAV_COMMAND_FALLBACK: {
          fallback = ProgressMapper::toCrossPoint(o.epub, {sync->xpath, sync->percentage}, o.renderer, a, b);
          break;
        }
        case XP_UI_READER_NAV_COMMAND_PREPARE_ANCHOR: {
          anchor.assign(href->data() + static_cast<uint32_t>(a), static_cast<uint32_t>(b));
          break;
        }
        case XP_UI_READER_NAV_COMMAND_MOVE_ANCHOR: {
          o.pendingAnchor = std::move(anchor);
          break;
        }
        case XP_UI_READER_NAV_COMMAND_LOG_UNRESOLVED: {
          LOG_DBG("ERS", "Could not resolve href: %s", href->c_str());
          break;
        }
        case XP_UI_READER_NAV_COMMAND_LOG_SAVED: {
          LOG_DBG("ERS", "Saved position [%d]: spine %d, page %d", a, o.currentSpineIndex, b);
          break;
        }
        case XP_UI_READER_NAV_COMMAND_LOG_RESTORED: {
          const auto& position = o.savedPositions[a];
          LOG_DBG("ERS", "Restoring position [%d]: spine %d, page %d", a, position.spineIndex, position.pageNumber);
          break;
        }
        case XP_UI_READER_NAV_COMMAND_LOG_NAVIGATED: {
          LOG_DBG("ERS", "Navigated to spine %d for href: %s", a, href->c_str());
          break;
        }
        case XP_UI_READER_NAV_COMMAND_SAVE_PROGRESS: {
          return EpubReaderUtils::saveProgress(*o.epub, spineArgument, pageArgument, countArgument,
                                               a ? std::optional<uint32_t>(static_cast<uint32_t>(b)) : std::nullopt);
          break;
        }
      }
      return 0;
    }
  } context{*this, a, b, c, href, sync, {}, {}, {}, {}};
  const XpUiReaderNavServices services{
      &context,
      [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t q, uint32_t a) { return static_cast<Context*>(p)->query(q, a); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); },
      href ? reinterpret_cast<const uint8_t*>(href->data()) : nullptr,
      href ? static_cast<uint32_t>(href->size()) : 0};
  const bool result = xp_ui_reader_navigation(&services, operation, a, b);
  assert(!context.lock);
  return result;
}
