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
constexpr int CLIP_SELECTION_PAGE_WINDOW = 3;

// Word i's font: headings and CSS font-size runs can differ from the section font.
int clippingWordFont(const GfxRenderer& renderer, const int fontId, const TextBlock& block, const uint16_t i) {
  return TextBlock::renderFontId(renderer, block.wordFontId(i, fontId), fontId);
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
  const auto range = xp_ui_reader_bookmark_range(page, pageCount);
  return {epub->calculateProgress(spineIndex, range.start), epub->calculateProgress(spineIndex, range.end)};
}

bool bookmarkMatchesProgress(const BookmarkEntry& bookmark, const int spineIndex, const int page, const int pageCount,
                             const ProgressRange& pageRange) {
  return xp_ui_reader_bookmark_matches({bookmark.percentage, bookmark.computedSpineIndex,
                                        bookmark.computedChapterProgress, bookmark.computedChapterPageCount},
                                       spineIndex, page, pageCount, {pageRange.start, pageRange.end}) != 0;
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

EpubReaderActivity::~EpubReaderActivity() { runBookLifetime(XP_UI_READER_LIFETIME_OPERATION_TEARDOWN); }

bool EpubReaderActivity::loadBook() { return runBookLifetime(XP_UI_READER_LIFETIME_OPERATION_LOAD); }

ChapterPosition EpubReaderActivity::chapterPosition() const {
  ChapterPosition result;
  runProjection(XP_UI_READER_PROJECTION_OPERATION_CHAPTER, 0, 0, 0, &result);
  return result;
}

int EpubReaderActivity::bookPercentFor(const ChapterPosition& position) const {
  return static_cast<int>(
      runProjection(XP_UI_READER_PROJECTION_OPERATION_BOOK_PERCENT, position.pageIndex, position.totalPages));
}

void EpubReaderActivity::openReaderMenu() { runMenuPolicy(XP_UI_READER_PARENT_OPERATION_OPEN, 0); }

// Runs on the render task, under its lock, right before every chapter build. The page turner's
// radio holds about 50 KB while it runs, and no heap figure taken before a build tells whether the
// build fits next to it (X3: with the radio up, 55 KB free and a 47 KB block, a 6 KB chapter ran
// out of memory). So the radio stops for every build and starts again once the chapter is built
// and its page is shown. A radio that did not stop within the module's 3 s, or a start still
// settling, usually lets go soon after: the build waits up to the Rust policy timeout, then is
// refused rather than run next to it. The stop finishes on the main loop (the module's tick) and
// the start on its own task; neither needs the render lock this task holds.
bool EpubReaderActivity::makeRoomForChapterBuild() { return runBuildPolicy(XP_UI_READER_BUILD_OPERATION_ROOM); }

bool EpubReaderActivity::buildTickHeapGate() { return runBuildPolicy(XP_UI_READER_BUILD_OPERATION_HEAP); }

void EpubReaderActivity::showBuildPopup() { runBuildPolicy(XP_UI_READER_BUILD_OPERATION_POPUP); }

// Debounced state of every button. Covers the frame between a raw release and its debounced
// release edge, which would otherwise run a build step before the page turn.
bool EpubReaderActivity::anyButtonPressed() const { return runBuildPolicy(XP_UI_READER_BUILD_OPERATION_BUTTONS); }

void EpubReaderActivity::backgroundBuildTick() { runBuildPolicy(XP_UI_READER_BUILD_OPERATION_BACKGROUND); }

struct ReaderChildCapture {
  uint16_t spine;
  int page;
  uint16_t count;
  uint32_t signature;
  const std::string& title;
  const std::string& author;
  const std::string& chapter;
};
namespace {
struct ReaderChildResources {
  std::optional<RenderLock> lock;
  std::optional<std::vector<std::unique_ptr<Page>>> pages;
  std::unique_ptr<Page> page;
  std::unique_ptr<Activity> activity;
  std::optional<std::string> chapter, title, author, href, path;
  std::optional<CrossPointPosition> position;
  std::optional<SavedProgressPosition> saved;
  std::optional<GfxRenderer::FrameBufferLoan> loan;
  decltype(std::declval<GfxRenderer&>().getFontCacheManager()) cache = nullptr;
  uint16_t spine = 0, count = 0, paragraph = UINT16_MAX;
  int firstPage = 0;
  uint32_t signature = 0;
  int margins[4]{};
};
}  // namespace

void EpubReaderActivity::openDictionaryWordSelect() {
  runChildPolicy(XP_UI_READER_CHILD_OPERATION_DICTIONARY, 0, 0, ClipSelectionActivity::Mode::Lookup);
}

void EpubReaderActivity::startClipSelection(int x, int y, ClipSelectionActivity::Mode mode) {
  runChildPolicy(XP_UI_READER_CHILD_OPERATION_SELECTION, x, y, mode);
}

void EpubReaderActivity::openFootnoteSelect(bool reopen) {
  runChildPolicy(XP_UI_READER_CHILD_OPERATION_FOOTNOTE, reopen);
}

void EpubReaderActivity::openClippings() { runChildPolicy(XP_UI_READER_CHILD_OPERATION_CLIPPINGS); }

bool EpubReaderActivity::runChildPolicy(uint8_t operation, int32_t a, int32_t b, ClipSelectionActivity::Mode mode,
                                        const ActivityResult* result, const ReaderChildCapture* capture) {
  ReaderChildResources resources;
  struct Context {
    EpubReaderActivity& o;
    ReaderChildResources& r;
    ClipSelectionActivity::Mode mode;
    const ActivityResult* result;
    const ReaderChildCapture* capture;
    uint32_t read(uint8_t field) {
      switch (static_cast<XpUiReaderChildField>(field)) {
        case XP_UI_READER_CHILD_FIELD_DICTIONARY_EMPTY:
          return SETTINGS.dictionaryName[0] == '\0';
        case XP_UI_READER_CHILD_FIELD_HAS_SECTION:
          return bool(o.section);
        case XP_UI_READER_CHILD_FIELD_HAS_BOOK:
          return bool(o.epub);
        case XP_UI_READER_CHILD_FIELD_PAGE:
          return o.section->currentPage;
        case XP_UI_READER_CHILD_FIELD_PAGE_COUNT:
          return o.section->pageCount;
        case XP_UI_READER_CHILD_FIELD_SPINE: {
          ModelLock lock(o);
          return o.currentSpineIndex;
        }
        case XP_UI_READER_CHILD_FIELD_BUILD_WIDTH: {
          ModelLock lock(o);
          return o.buildViewportWidth;
        }
        case XP_UI_READER_CHILD_FIELD_BUILD_HEIGHT: {
          ModelLock lock(o);
          return o.buildViewportHeight;
        }
        case XP_UI_READER_CHILD_FIELD_WINDOW:
          return CLIP_SELECTION_PAGE_WINDOW;
        case XP_UI_READER_CHILD_FIELD_HAS_CLIPPINGS:
          return CLIPPINGS.hasClippings();
        case XP_UI_READER_CHILD_FIELD_SCREEN_MARGIN:
          return SETTINGS.screenMargin;
        case XP_UI_READER_CHILD_FIELD_CANCELLED:
          return result->isCancelled;
        case XP_UI_READER_CHILD_FIELD_RESULT_ACTION:
          return static_cast<uint32_t>(std::get<ClippingResult>(result->data).action);
        case XP_UI_READER_CHILD_FIELD_START_PAGE_OFFSET:
          return std::get<ClippingResult>(result->data).startPageOffset;
        case XP_UI_READER_CHILD_FIELD_END_PAGE_OFFSET:
          return std::get<ClippingResult>(result->data).endPageOffset;
        case XP_UI_READER_CHILD_FIELD_CAPTURED_SPINE:
          return capture ? capture->spine : r.spine;
        case XP_UI_READER_CHILD_FIELD_CAPTURED_PAGE:
          return capture ? capture->page : r.firstPage;
        case XP_UI_READER_CHILD_FIELD_CAPTURED_PAGE_COUNT:
          return capture ? capture->count : r.count;
        case XP_UI_READER_CHILD_FIELD_HAS_PROGRESS_RESULT:
          return std::get_if<ProgressChangeResult>(&result->data) != nullptr;
        case XP_UI_READER_CHILD_FIELD_FOOTNOTE_COUNT:
          return o.currentPageMetadata.footnotes().size();
        case XP_UI_READER_CHILD_FIELD_HAS_CREDENTIALS:
          return KOREADER_STORE.hasCredentials();
        case XP_UI_READER_CHILD_FIELD_NEXT_PAGE: {
          ModelLock lock(o);
          return o.nextPageNumber;
        }
        case XP_UI_READER_CHILD_FIELD_CACHED_PAGE_COUNT: {
          ModelLock lock(o);
          return o.cachedChapterTotalPageCount;
        }
        case XP_UI_READER_CHILD_FIELD_FOOTNOTE_DEPTH: {
          ModelLock lock(o);
          return o.footnoteDepth;
        }
        case XP_UI_READER_CHILD_FIELD_SHOW_DICTIONARY: {
          ModelLock lock(o);
          return o.showDictionaryMessage;
        }
        case XP_UI_READER_CHILD_FIELD_DICTIONARY_TIME: {
          ModelLock lock(o);
          return o.dictionaryMessageTime;
        }
        case XP_UI_READER_CHILD_FIELD_SHOW_BOOKMARK: {
          ModelLock lock(o);
          return o.showBookmarkMessage;
        }
        case XP_UI_READER_CHILD_FIELD_BOOKMARK_TIME: {
          ModelLock lock(o);
          return o.bookmarkMessageTime;
        }
        case XP_UI_READER_CHILD_FIELD_SHOW_CLIPPING: {
          ModelLock lock(o);
          return o.showClippingMessage;
        }
        case XP_UI_READER_CHILD_FIELD_CLIPPING_TIME: {
          ModelLock lock(o);
          return o.clippingMessageTime;
        }
        case XP_UI_READER_CHILD_FIELD_PENDING_SYNC_ERROR: {
          ModelLock lock(o);
          return o.pendingSyncSaveError;
        }
        case XP_UI_READER_CHILD_FIELD_STRIPS_ENABLED:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          return 1;
#else
          return 0;
#endif
        case XP_UI_READER_CHILD_FIELD_CLIPPING_MESSAGE:
          return 0;
      }
      return 0;
    }
    void write(uint8_t field, uint32_t value) {
      switch (static_cast<XpUiReaderChildField>(field)) {
        case XP_UI_READER_CHILD_FIELD_PAGE: {
          ModelLock lock(o);
          o.section->currentPage = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_NEXT_PAGE: {
          ModelLock lock(o);
          o.nextPageNumber = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_SHOW_DICTIONARY: {
          ModelLock lock(o);
          o.showDictionaryMessage = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_DICTIONARY_TIME: {
          ModelLock lock(o);
          o.dictionaryMessageTime = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_SHOW_BOOKMARK: {
          ModelLock lock(o);
          o.showBookmarkMessage = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_BOOKMARK_TIME: {
          ModelLock lock(o);
          o.bookmarkMessageTime = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_SHOW_CLIPPING: {
          ModelLock lock(o);
          o.showClippingMessage = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_CLIPPING_TIME: {
          ModelLock lock(o);
          o.clippingMessageTime = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_PENDING_SYNC_ERROR: {
          ModelLock lock(o);
          o.pendingSyncSaveError = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_CAPTURED_SPINE: {
          r.spine = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_CAPTURED_PAGE: {
          r.firstPage = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_CAPTURED_PAGE_COUNT: {
          r.count = value;
          break;
        }
        case XP_UI_READER_CHILD_FIELD_CLIPPING_MESSAGE: {
          constexpr StrId messages[] = {StrId::STR_CLIPPING_REMOVED,       StrId::STR_CLIPPING_DELETE_FAILED,
                                        StrId::STR_CLIPPING_LIMIT_REACHED, StrId::STR_CLIPPING_SAVED,
                                        StrId::STR_CLIPPING_EXPORT_FAILED, StrId::STR_CLIPPING_FAILED};
          if (value < std::size(messages)) {
            ModelLock lock(o);
            o.clippingMessage = messages[value];
          }
          break;
        }
        default:
          break;
      }
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderChildCommand>(command)) {
        case XP_UI_READER_CHILD_COMMAND_CLOCK:
          return millis();
          break;
        case XP_UI_READER_CHILD_COMMAND_UPDATE:
          o.requestUpdate();
          break;
        case XP_UI_READER_CHILD_COMMAND_LOCK:
          r.lock.emplace();
          break;
        case XP_UI_READER_CHILD_COMMAND_UNLOCK:
          r.lock.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_RELEASE_STRIPS:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          o.stripBuffers.releaseIdle();
#endif
          break;
        case XP_UI_READER_CHILD_COMMAND_PAGES_BEGIN:
          r.pages.emplace();
          break;
        case XP_UI_READER_CHILD_COMMAND_PAGES_END:
          r.pages.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_RESERVE_PAGES:
          r.pages->reserve(a);
          break;
        case XP_UI_READER_CHILD_COMMAND_PAGE_LOAD:
          r.page = o.section->loadPage(a);
          return bool(r.page);
          break;
        case XP_UI_READER_CHILD_COMMAND_PAGE_END:
          r.page.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_APPEND_PAGE:
          r.pages->push_back(std::move(r.page));
          break;
        case XP_UI_READER_CHILD_COMMAND_HIT:
          return o.clippingAtPoint(*r.page, a, b);
          break;
        case XP_UI_READER_CHILD_COMMAND_REMOVE_CLIPPING:
          return CLIPPINGS.removeClippingAt(a);
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_REMOVE_FAILED:
          LOG_ERR("CLIP", "Failed to delete highlighted clipping %d", static_cast<int>(a));
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_NO_PAGE:
          LOG_ERR("CLIP", "Failed to load page %d for clipping selection", static_cast<int>(a));
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_NO_VIEWPORT:
          LOG_ERR("CLIP", "Cannot anchor clipping before the reader viewport is initialized");
          break;
        case XP_UI_READER_CHILD_COMMAND_SIGNATURE:
          r.signature =
              readerRenderSpecSignature(SETTINGS.readerRenderSpec(o.buildViewportWidth, o.buildViewportHeight));
          return r.signature;
          break;
        case XP_UI_READER_CHILD_COMMAND_MARGINS:
          o.renderer.getOrientedViewableTRBL(&r.margins[0], &r.margins[1], &r.margins[2], &r.margins[3]);
          break;
        case XP_UI_READER_CHILD_COMMAND_TOC_INDEX:
          return o.epub->getTocIndexForSpineIndex(o.currentSpineIndex);
          break;
        case XP_UI_READER_CHILD_COMMAND_CHAPTER_BEGIN:
          r.chapter.emplace();
          if (a >= 0) *r.chapter = o.epub->getTocItem(a).title;
          break;
        case XP_UI_READER_CHILD_COMMAND_CHAPTER_END:
          r.chapter.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_TITLE_BEGIN:
          r.title.emplace(o.epub->getTitle());
          break;
        case XP_UI_READER_CHILD_COMMAND_TITLE_END:
          r.title.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_AUTHOR_BEGIN:
          r.author.emplace(o.epub->getAuthor());
          break;
        case XP_UI_READER_CHILD_COMMAND_AUTHOR_END:
          r.author.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_NEW_SELECTION:
          r.activity = makeUniqueNoThrow<ClipSelectionActivity>(o.renderer, o.mappedInput, std::move(*r.pages),
                                                                r.margins[3] + SETTINGS.screenMargin,
                                                                r.margins[0] + SETTINGS.screenMargin, a, b, mode);
          return bool(r.activity);
          break;
        case XP_UI_READER_CHILD_COMMAND_LAUNCH_SELECTION:
          o.startActivityForResult(
              std::move(r.activity),
              [owner = &o, spineIndex = r.spine, pageNumber = r.firstPage, pageCount = r.count,
               layoutSignature = r.signature, bookTitle = std::move(*r.title), author = std::move(*r.author),
               chapterTitle = std::move(*r.chapter)](const ActivityResult& result) {
                const ReaderChildCapture capture{spineIndex, pageNumber, pageCount,   layoutSignature,
                                                 bookTitle,  author,     chapterTitle};
                owner->runChildPolicy(XP_UI_READER_CHILD_OPERATION_SELECTION_RESULT, 0, 0,
                                      ClipSelectionActivity::Mode::Clip, &result, &capture);
              });
          break;
        case XP_UI_READER_CHILD_COMMAND_DROP_ACTIVITY:
          r.activity.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_SELECTION_OOM:
          LOG_ERR("CLIP", "Failed to allocate clipping selection activity");
          break;
        case XP_UI_READER_CHILD_COMMAND_READ_VISIBLE_OFFSET:
          o.currentPageVisibleOffset = o.section->getVisibleTextOffsetForPage(a);
          break;
        case XP_UI_READER_CHILD_COMMAND_ADD_BOOKMARK:
          o.addBookmark();
          break;
        case XP_UI_READER_CHILD_COMMAND_LOOKUP_PAGE:
          r.page = o.section ? o.section->loadPage(o.section->currentPage) : nullptr;
          return bool(r.page);
          break;
        case XP_UI_READER_CHILD_COMMAND_NEW_LOOKUP:
          r.activity = makeUniqueNoThrow<DictionaryWordSelectActivity>(
              o.renderer, o.mappedInput, std::move(r.page), r.margins[3] + SETTINGS.screenMargin,
              r.margins[0] + SETTINGS.screenMargin, std::get<ClippingResult>(result->data).text);
          return bool(r.activity);
          break;
        case XP_UI_READER_CHILD_COMMAND_LAUNCH_LOOKUP:
          o.startActivityForResult(std::move(r.activity), [owner = &o](const ActivityResult& result) {
            owner->runChildPolicy(XP_UI_READER_CHILD_OPERATION_LOOKUP_RESULT, 0, 0, ClipSelectionActivity::Mode::Lookup,
                                  &result);
          });
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_LOOKUP_PAGE:
          LOG_ERR("CLIP", "Failed to load lookup page");
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_LOOKUP_OOM:
          LOG_ERR("CLIP", "OOM: dictionary lookup activity");
          break;
        case XP_UI_READER_CHILD_COMMAND_PARAGRAPH_INDEX:
          r.paragraph = o.section->getParagraphIndexForPage(a).value_or(UINT16_MAX);
          return r.paragraph;
          break;
        case XP_UI_READER_CHILD_COMMAND_ADD_CLIPPING: {
          const auto& c = std::get<ClippingResult>(result->data);
          return static_cast<uint32_t>(CLIPPINGS.addClipping(
              capture->spine, a, b, capture->count, c.startWordIndex, c.endWordIndex, c.wordCount,
              capture->chapter.c_str(), r.paragraph, c.text, capture->signature, c.startOffset, c.endOffset));
        } break;
        case XP_UI_READER_CHILD_COMMAND_EXPORT_CLIPPING:
          return ClippingsManager::saveClipping(capture->title, capture->author, capture->chapter, a,
                                                std::get<ClippingResult>(result->data).text);
          break;
        case XP_UI_READER_CHILD_COMMAND_HREF_BEGIN:
          r.href.emplace();
          break;
        case XP_UI_READER_CHILD_COMMAND_HREF_END:
          r.href.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_COPY_FIRST_HREF:
          *r.href = o.currentPageMetadata.footnotes()[0].href;
          break;
        case XP_UI_READER_CHILD_COMMAND_NAVIGATE_HREF:
          o.navigateToHref(*r.href, true);
          break;
        case XP_UI_READER_CHILD_COMMAND_NEW_FOOTNOTE:
          r.activity = makeUniqueNoThrow<EpubReaderFootnoteSelectActivity>(o.renderer, o.mappedInput, std::move(r.page),
                                                                           r.margins[3] + SETTINGS.screenMargin,
                                                                           r.margins[0] + SETTINGS.screenMargin);
          return bool(r.activity);
          break;
        case XP_UI_READER_CHILD_COMMAND_LAUNCH_FOOTNOTE:
          o.startActivityForResult(std::move(r.activity), [owner = &o, reopen = bool(a)](const ActivityResult& result) {
            owner->runChildPolicy(XP_UI_READER_CHILD_OPERATION_FOOTNOTE_RESULT, reopen, 0,
                                  ClipSelectionActivity::Mode::Clip, &result);
          });
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_FOOTNOTE_OOM:
          LOG_ERR("ERA", "OOM: EpubReaderFootnoteSelectActivity");
          break;
        case XP_UI_READER_CHILD_COMMAND_OPEN_MENU:
          o.openReaderMenu();
          break;
        case XP_UI_READER_CHILD_COMMAND_NAVIGATE_RESULT_HREF:
          o.navigateToHref(std::get<FootnoteResult>(result->data).href, true);
          break;
        case XP_UI_READER_CHILD_COMMAND_NEW_CLIPPINGS:
          r.activity = makeUniqueNoThrow<EpubReaderClippingListActivity>(o.renderer, o.mappedInput);
          return bool(r.activity);
          break;
        case XP_UI_READER_CHILD_COMMAND_LAUNCH_CLIPPINGS:
          o.startActivityForResult(std::move(r.activity), [owner = &o](const ActivityResult& result) {
            owner->runChildPolicy(XP_UI_READER_CHILD_OPERATION_CLIPPINGS_RESULT, 0, 0,
                                  ClipSelectionActivity::Mode::Clip, &result);
          });
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_CLIPPINGS_OOM:
          LOG_ERR("CLIP", "OOM: clipping list");
          break;
        case XP_UI_READER_CHILD_COMMAND_APPLY_RESULT_PROGRESS:
          o.applyProgressChange(std::get<ProgressChangeResult>(result->data));
          break;
        case XP_UI_READER_CHILD_COMMAND_FLUSH_SESSION:
          o.flushReaderSession();
          break;
        case XP_UI_READER_CHILD_COMMAND_LAUNCH_EVENTS_ONLY:
          activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
              o.renderer, o.mappedInput, o.epub->getPath(), o.getCurrentPosition(), SavedProgressPosition{}, "", true));
          break;
        case XP_UI_READER_CHILD_COMMAND_ESTIMATE:
          return o.section->estimatedTotalPages();
          break;
        case XP_UI_READER_CHILD_COMMAND_POSITION_BEGIN:
          r.position.emplace(o.getCurrentPosition());
          break;
        case XP_UI_READER_CHILD_COMMAND_POSITION_END:
          r.position.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_SAVED_POSITION_BEGIN:
          r.saved.emplace();
          break;
        case XP_UI_READER_CHILD_COMMAND_SAVED_POSITION_END:
          r.saved.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_PATH_BEGIN:
          r.path.emplace(o.epub->getPath());
          break;
        case XP_UI_READER_CHILD_COMMAND_PATH_END:
          r.path.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_SAVE_PROGRESS:
          return o.saveProgress(o.currentSpineIndex, a, b);
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_SYNC_SAVE_FAILED:
          LOG_ERR("KOSync", "Aborting sync because current progress could not be saved");
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_SYNC_BEFORE:
          LOG_DBG("KOSync", "Releasing epub for sync (heap before: %u)", (unsigned)ESP.getFreeHeap());
          break;
        case XP_UI_READER_CHILD_COMMAND_DISCARD_OVERLAY:
          o.discardOverlayPage();
          break;
        case XP_UI_READER_CHILD_COMMAND_RELEASE_IMAGES:
          ImageBlock::releaseRenderCache();
          break;
        case XP_UI_READER_CHILD_COMMAND_CLEAR_EXTRACTOR:
          ImageBlock::setExtractor(nullptr, nullptr);
          break;
        case XP_UI_READER_CHILD_COMMAND_DROP_SECTION:
          o.section.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_CACHE_BEGIN:
          r.cache = o.renderer.getFontCacheManager();
          return r.cache != nullptr;
          break;
        case XP_UI_READER_CHILD_COMMAND_RELEASE_CACHE:
          r.cache->releaseSdFontCaches();
          break;
        case XP_UI_READER_CHILD_COMMAND_CACHE_END:
          r.cache = nullptr;
          break;
        case XP_UI_READER_CHILD_COMMAND_LOAN_BEGIN:
          r.loan.emplace(o.renderer);
          break;
        case XP_UI_READER_CHILD_COMMAND_LOAN_END:
          r.loan.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_MAP_PROGRESS:
          *r.saved = ProgressMapper::toSavedProgress(o.epub, *r.position);
          break;
        case XP_UI_READER_CHILD_COMMAND_SAVE_LINKS:
          o.saveLinkStack();
          break;
        case XP_UI_READER_CHILD_COMMAND_DROP_BOOK:
          o.epub.reset();
          break;
        case XP_UI_READER_CHILD_COMMAND_LOG_SYNC_AFTER:
          LOG_DBG("KOSync", "Epub released (heap after: %u)", (unsigned)ESP.getFreeHeap());
          break;
        case XP_UI_READER_CHILD_COMMAND_LAUNCH_SYNC:
          activityManager.replaceActivity(std::make_unique<KOReaderSyncActivity>(
              o.renderer, o.mappedInput, *r.path, *r.position, std::move(*r.saved), std::move(*r.chapter)));
          break;
        case XP_UI_READER_CHILD_COMMAND_DICTIONARY:
          o.openDictionaryWordSelect();
          break;
      }
      return 0;
    }
  } context{*this, resources, mode, result, capture};
  const XpUiReaderChildServices services{
      &context, [](void* c, uint8_t f) -> uint32_t { return static_cast<Context*>(c)->read(f); },
      [](void* c, uint8_t f, uint32_t v) { static_cast<Context*>(c)->write(f, v); },
      [](void* c, uint8_t id, int32_t a, int32_t b) -> uint32_t {
        return static_cast<Context*>(c)->command(id, a, b);
      }};
  return xp_ui_reader_child(&services, operation, a, b, static_cast<uint8_t>(mode));
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
  if (consumeRejectedInput()) return;
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
          if (!o.consumeRejectedInput()) o.handleOverlayInput();
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
  return static_cast<int>(runProjection(XP_UI_READER_PROJECTION_OPERATION_TOC));
}

void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  runMenuPolicy(XP_UI_READER_PARENT_OPERATION_CONFIRM, static_cast<int32_t>(action));
}

unsigned long EpubReaderActivity::confirmLongPressThreshold() const {
  return runProjection(XP_UI_READER_PROJECTION_OPERATION_CONFIRM_THRESHOLD);
}

bool EpubReaderActivity::launchKOReaderSync() { return runChildPolicy(XP_UI_READER_CHILD_OPERATION_SYNC); }

void EpubReaderActivity::applyInitialOrientation() {
  runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_INITIAL_ORIENTATION, 0);
}

void EpubReaderActivity::applyOrientation(const uint8_t orientation) {
  runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_ORIENTATION, orientation);
}

void EpubReaderActivity::toggleAutoPageTurn(const uint8_t selectedPageTurnOption) {
  runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_AUTO_TURN, selectedPageTurnOption);
}

bool EpubReaderActivity::pageTurn(bool isForwardTurn) {
  return runProjection(XP_UI_READER_PROJECTION_OPERATION_TURN, isForwardTurn) != 0;
}

bool EpubReaderActivity::skipPages(int amount) {
  return runProjection(XP_UI_READER_PROJECTION_OPERATION_SKIP, amount) != 0;
}

// Failed protected open: show the standard option dialog (wrapped message)
// instead of silently falling back to the previous screen. Exact error
// strings are set by openProtectedBook (ContentProtection.cpp).
bool EpubReaderActivity::handleLoadFailure() { return runRecoveryPolicy(XP_UI_READER_RECOVERY_OPERATION_LOAD); }

void EpubReaderActivity::beginLoanTimeSync() { runRecoveryPolicy(XP_UI_READER_RECOVERY_OPERATION_BEGIN); }

void EpubReaderActivity::render(RenderLock&& lock) {
  if (!runRecoveryPolicy(XP_UI_READER_RECOVERY_OPERATION_RENDER)) ReaderActivity::render(std::move(lock));
  // Keep the overlay decision and publication in the same model transition.
  ModelLock modelLock(*this);
  if (overlay == Overlay::None) publishRustScene({});
}

bool EpubReaderActivity::runRecoveryPolicy(uint8_t operation, int32_t a, int32_t b, const ActivityResult* result) {
  struct Context {
    EpubReaderActivity& o;
    const ActivityResult* result;
    uint32_t query(uint8_t query) {
      switch (static_cast<XpUiReaderRecoveryQuery>(query)) {
        case XP_UI_READER_RECOVERY_QUERY_ERROR_EMPTY:
          return o.loadProtectionError.empty();
        case XP_UI_READER_RECOVERY_QUERY_ERROR_EXPIRED:
          return o.loadProtectionError == "access expired";
        case XP_UI_READER_RECOVERY_QUERY_ERROR_UNVERIFIED:
          return o.loadProtectionError == "loan date unverified";
        case XP_UI_READER_RECOVERY_QUERY_CANCELLED:
          assert(result);
          return result->isCancelled;
        case XP_UI_READER_RECOVERY_QUERY_POPUP_ACTIVE:
          return o.loadFailurePopup.isActive();
      }
      return 0;
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderRecoveryCommand>(command)) {
        case XP_UI_READER_RECOVERY_COMMAND_SHOW_FAILURE: {
          const StrId messages[] = {StrId::STR_DRM_PROTECTED_FILE, StrId::STR_LOAN_EXPIRED,
                                    StrId::STR_LOAN_TIME_UNVERIFIED};
          assert(a >= 0 && a < 3);
          const bool offerSync = b != 0;
          const char* options[2] = {I18N.get(offerSync ? StrId::STR_CLOCK_SYNC_NOW : StrId::STR_OK_BUTTON),
                                    I18N.get(StrId::STR_OK_BUTTON)};
          o.loadFailurePopup.showMessage(
              "", I18N.get(messages[a]), options, offerSync ? 2 : 1, 0, [owner = &o, offerSync](const int index) {
                owner->runRecoveryPolicy(XP_UI_READER_RECOVERY_OPERATION_LOAD_RESULT, index, offerSync);
              });
          break;
        }
        case XP_UI_READER_RECOVERY_COMMAND_UPDATE:
          o.requestUpdate();
          break;
        case XP_UI_READER_RECOVERY_COMMAND_FINISH:
          o.finish();
          break;
        case XP_UI_READER_RECOVERY_COMMAND_START_WIFI: {
          auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(o.renderer, o.mappedInput);
          if (!wifi) return false;
          o.startActivityForResult(std::move(wifi), [owner = &o](const ActivityResult& result) {
            owner->runRecoveryPolicy(XP_UI_READER_RECOVERY_OPERATION_WIFI_RESULT, 0, 0, &result);
          });
          return true;
        }
        case XP_UI_READER_RECOVERY_COMMAND_LOG_OOM:
          LOG_ERR("ERS", "OOM: Wi-Fi selection for loan time sync");
          break;
        case XP_UI_READER_RECOVERY_COMMAND_CONNECTED:
          return WiFi.status() == WL_CONNECTED;
        case XP_UI_READER_RECOVERY_COMMAND_PAINT_SYNC:
          GUI.drawPopup(o.renderer, tr(STR_SYNCING_TIME));
          break;
        case XP_UI_READER_RECOVERY_COMMAND_SYNC:
          return trustedtime::syncNow(a);
        case XP_UI_READER_RECOVERY_COMMAND_DISCONNECT:
          WiFi.disconnect(a != 0);
          break;
        case XP_UI_READER_RECOVERY_COMMAND_DELAY:
          delay(a);
          break;
        case XP_UI_READER_RECOVERY_COMMAND_SHOW_RETRY: {
          const char* options[2] = {I18N.get(StrId::STR_RETRY), I18N.get(StrId::STR_OK_BUTTON)};
          o.loadFailurePopup.showMessage(
              "", I18N.get(StrId::STR_CLOCK_SYNC_FAIL), options, 2, 0, [owner = &o](const int index) {
                owner->runRecoveryPolicy(XP_UI_READER_RECOVERY_OPERATION_RETRY_RESULT, index);
              });
          break;
        }
        case XP_UI_READER_RECOVERY_COMMAND_SAVE_PATH:
          APP_STATE.openEpubPath = o.bookPath;
          APP_STATE.saveToFile();
          break;
        case XP_UI_READER_RECOVERY_COMMAND_RESTART:
          silentRestartToReader();
          break;
        case XP_UI_READER_RECOVERY_COMMAND_REOPEN:
          activityManager.goToReader(o.bookPath);
          break;
        case XP_UI_READER_RECOVERY_COMMAND_CLEAR:
          o.renderer.clearScreen();
          break;
        case XP_UI_READER_RECOVERY_COMMAND_PAINT_FAILURE:
          o.loadFailurePopup.processRender(o.renderer, o.mappedInput);
          break;
        case XP_UI_READER_RECOVERY_COMMAND_BASE_RENDER:
          break;
      }
      return 0;
    }
  } context{*this, result};
  const XpUiReaderRecoveryServices services{
      &context, [](void* c, uint8_t q) -> uint32_t { return static_cast<Context*>(c)->query(q); },
      [](void* c, uint8_t command, int32_t a, int32_t b) -> uint32_t {
        return static_cast<Context*>(c)->command(command, a, b);
      }};
  return xp_ui_reader_recovery(&services, operation, a, b) != 0;
}

bool EpubReaderActivity::remoteShortcut(bool readerMenu) {
  return runProjection(XP_UI_READER_PROJECTION_OPERATION_REMOTE, readerMenu) != 0;
}

bool EpubReaderActivity::isAtEndOfBook() const { return runProjection(XP_UI_READER_PROJECTION_OPERATION_AT_END) != 0; }

void EpubReaderActivity::onReturnFromEndOfBook() { runProjection(XP_UI_READER_PROJECTION_OPERATION_RETURN_END); }

// A started build runs to the end of the chapter, one bounded step per idle loop frame, so the
// parser and its page LUT are freed soon (and a radio stopped for the build comes back with no
// parser alive). Not while the radio holds the heap, and not in a spine whose background build
// failed.
bool EpubReaderActivity::backgroundBuildWanted() const { return runBuildPolicy(XP_UI_READER_BUILD_OPERATION_WANTED); }

bool EpubReaderActivity::skipLoopDelay() { return runBuildPolicy(XP_UI_READER_BUILD_OPERATION_DELAY); }

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

void EpubReaderActivity::onEndOfBookRendered() { runProjection(XP_UI_READER_PROJECTION_OPERATION_RENDERED_END); }

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
  return runClippingPolicy(XP_UI_READER_CLIP_OPERATION_HIT, page, {x, y, 0, 0, 0});
}

void EpubReaderActivity::drawClippingHighlights(const Page& page, const int fontId, const int orientedMarginTop,
                                                const int orientedMarginLeft) const {
  runClippingPolicy(XP_UI_READER_CLIP_OPERATION_DRAW, page, {0, 0, fontId, orientedMarginTop, orientedMarginLeft});
}

int EpubReaderActivity::runClippingPolicy(uint8_t operation, const Page& page, const XpUiReaderClipInput& input) const {
  struct BlockView {
    TextBlock value;
    explicit BlockView(const PageElementView& line) : value(line.textBlock()) {}
  };
  struct Context {
    EpubReaderActivity& o;
    const Page& page;
    std::optional<PageElementView> line;
    std::optional<BlockView> block;
    int margins[4]{};
    unsigned long started = 0;
    uint32_t query(uint8_t query, uint32_t index) {
      switch (static_cast<XpUiReaderClipQuery>(query)) {
        case XP_UI_READER_CLIP_QUERY_HAS_SECTION:
          return bool(o.section);
        case XP_UI_READER_CLIP_QUERY_HAS_CLIPPINGS:
          return CLIPPINGS.hasClippings();
        case XP_UI_READER_CLIP_QUERY_PAGE:
          return o.section->currentPage;
        case XP_UI_READER_CLIP_QUERY_PAGE_COUNT:
          return o.section->pageCount;
        case XP_UI_READER_CLIP_QUERY_SPINE: {
          ModelLock lock(o);
          return o.currentSpineIndex;
        }
        case XP_UI_READER_CLIP_QUERY_ELEMENT_COUNT:
          return page.elementCount();
        case XP_UI_READER_CLIP_QUERY_WORD_COUNT:
          return block->value.wordCount();
        case XP_UI_READER_CLIP_QUERY_VISIBLE:
          return clippingText::hasVisibleText(block->value.wordText(index));
        case XP_UI_READER_CLIP_QUERY_WORD_X:
          return block->value.wordXpos(index);
        case XP_UI_READER_CLIP_QUERY_LINE_X:
          return line->xPos;
        case XP_UI_READER_CLIP_QUERY_LINE_Y:
          return line->yPos;
        case XP_UI_READER_CLIP_QUERY_TOP:
          return margins[0];
        case XP_UI_READER_CLIP_QUERY_LEFT:
          return margins[3];
        case XP_UI_READER_CLIP_QUERY_SCREEN_MARGIN:
          return SETTINGS.screenMargin;
        case XP_UI_READER_CLIP_QUERY_CLIP_COUNT:
          return CLIPPINGS.clippingCount();
        case XP_UI_READER_CLIP_QUERY_A4:
          return BoardConfig::isEegoA4();
        case XP_UI_READER_CLIP_QUERY_BW:
          return o.renderer.getRenderMode() == GfxRenderer::BW;
        case XP_UI_READER_CLIP_QUERY_EM_SPACE:
          return clippingText::hasEmSpacePrefix(block->value.wordText(index));
        case XP_UI_READER_CLIP_QUERY_SOURCE_START:
          return block->value.wordSourceRange(index).start;
        case XP_UI_READER_CLIP_QUERY_SOURCE_END:
          return block->value.wordSourceRange(index).end;
      }
      return 0;
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderClipCommand>(command)) {
        case XP_UI_READER_CLIP_COMMAND_MARGINS:
          o.renderer.getOrientedViewableTRBL(&margins[0], &margins[1], &margins[2], &margins[3]);
          break;
        case XP_UI_READER_CLIP_COMMAND_READER_FONT:
          return SETTINGS.getReaderFontId();
        case XP_UI_READER_CLIP_COMMAND_SIGNATURE:
          return readerRenderSpecSignature(SETTINGS.readerRenderSpec(o.buildViewportWidth, o.buildViewportHeight));
        case XP_UI_READER_CLIP_COMMAND_ELEMENT_BEGIN:
          line.emplace(page.element(a));
          return line->getTag() == TAG_PageLine;
        case XP_UI_READER_CLIP_COMMAND_ELEMENT_END:
          line.reset();
          break;
        case XP_UI_READER_CLIP_COMMAND_BLOCK_BEGIN:
          block.emplace(*line);
          return block->value.valid();
        case XP_UI_READER_CLIP_COMMAND_BLOCK_END:
          block.reset();
          break;
        case XP_UI_READER_CLIP_COMMAND_WORD_FONT:
          return clippingWordFont(o.renderer, b, block->value, a);
        case XP_UI_READER_CLIP_COMMAND_MEASURE:
          return o.renderer.getTextAdvanceX(
              b, block->value.wordText(a),
              static_cast<EpdFontFamily::Style>(block->value.wordStyle(a) & ~EpdFontFamily::UNDERLINE),
              block->value.getBlockStyle().characterSpacing);
        case XP_UI_READER_CLIP_COMMAND_EM_WIDTH:
          return o.renderer.getTextAdvanceX(
              b, "\xe2\x80\x83",
              static_cast<EpdFontFamily::Style>(block->value.wordStyle(a) & ~EpdFontFamily::UNDERLINE));
        case XP_UI_READER_CLIP_COMMAND_LINE_FONT:
          return TextBlock::renderFontId(o.renderer, block->value.blockFontId(a), a);
        case XP_UI_READER_CLIP_COMMAND_RUBY:
          return block->value.getRubyShift(o.renderer.getFontAscenderSize(a));
        case XP_UI_READER_CLIP_COMMAND_Y_OFFSET:
          return block->value.wordYOffset(o.renderer, b, a);
        case XP_UI_READER_CLIP_COMMAND_LINE_HEIGHT:
          return o.renderer.getLineHeight(a);
        case XP_UI_READER_CLIP_COMMAND_PIXEL:
          o.renderer.drawPixel(a, b, false);
          break;
        case XP_UI_READER_CLIP_COMMAND_CLOCK_START:
          started = millis();
          break;
        case XP_UI_READER_CLIP_COMMAND_LOG:
          LOG_DBG("CLIP", "Highlight pass: chapter_clips=%u highlighted_words=%u time=%lums", static_cast<unsigned>(a),
                  static_cast<unsigned>(b), millis() - started);
          break;
      }
      return 0;
    }
  } context{const_cast<EpubReaderActivity&>(*this), page, {}, {}};
  const XpUiReaderClipServices services{
      &context,
      [](void* c, uint8_t query, uint32_t index) -> uint32_t { return static_cast<Context*>(c)->query(query, index); },
      [](void* c, uint8_t command, int32_t a, int32_t b) -> uint32_t {
        return static_cast<Context*>(c)->command(command, a, b);
      },
      [](void* c, const XpUiReaderClipRect* rect) {
        static_cast<Context*>(c)->o.renderer.fillRectDither(rect->x, rect->y, rect->width, rect->height,
                                                            Color::LightGray);
      },
      [](void*, uint32_t index, XpUiReaderClipRecord* out) {
        const auto& c = *CLIPPINGS.clippingAt(index);
        *out = {c.spineIndex,   c.startPage,       c.endPage,     c.pageCount, c.startWordIndex,
                c.endWordIndex, c.layoutSignature, c.startOffset, c.endOffset};
      }};
  return xp_ui_reader_clipping(&services, operation, &input);
}

void EpubReaderActivity::renderStatusBar() const { readerViewValue(XP_UI_READER_VALUE_OPERATION_STATUS); }

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
  static_assert(CrossPointSettings::READER_MENU_TOOLBAR == 1);
  return xp_ui_reader_toolbar_style(SETTINGS.readerMenuStyle);
}

std::string EpubReaderActivity::currentChapterTitle() const {
  return readerViewValue(XP_UI_READER_VALUE_OPERATION_CHAPTER);
}

std::string EpubReaderActivity::textRowName(int row) const {
  return readerViewValue(XP_UI_READER_VALUE_OPERATION_TEXT_NAME, row);
}

std::string EpubReaderActivity::textRowValue(int row) const {
  return readerViewValue(XP_UI_READER_VALUE_OPERATION_TEXT_VALUE, row);
}

// Live apply: persist, re-paginate, and let renderBook() redraw the page with
// the open panel back on top -- the book itself is the preview.
void EpubReaderActivity::applyTextSettingLive() { runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_LIVE_TEXT, 0); }

// Settings-style option pickers for the Text panel's enum rows. Every
// selection applies immediately to the page under the sheet.
void EpubReaderActivity::showTextRowPopup(const int row) {
  runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_POPUP, row);
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
        if (o.toolbarUi->render()) {
          o.publishRustScene({});
        } else {
          o.rejectRustScene();
          LOG_ERR("RUI", "Cannot build reader toolbar");
        }
      }};
  xp_ui_reader_overlay_view(&services);
}

void EpubReaderActivity::cancelRejectedInput(const XpUiInput& input) {
  if (input.released & XP_UI_BACK) runOverlayPolicy(XP_UI_READER_OVERLAY_OPERATION_CANCEL);
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

void EpubReaderActivity::applyReaderTextSettings() { runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_TEXT, 0); }

// The More panel carries everything the classic list menu offers except the
// two entries that have their own tool (chapters -> Contents, text -> Text).
void EpubReaderActivity::buildMoreActions() { runMenuPolicy(XP_UI_READER_PARENT_OPERATION_BUILD_MORE, 0); }

std::string EpubReaderActivity::moreRowName(int row) const {
  return readerViewValue(XP_UI_READER_VALUE_OPERATION_MORE_NAME, row);
}

std::string EpubReaderActivity::moreRowValue(int row) const {
  return readerViewValue(XP_UI_READER_VALUE_OPERATION_MORE_VALUE, row);
}

void EpubReaderActivity::activateMoreRow(int row) { runMenuPolicy(XP_UI_READER_PARENT_OPERATION_ACTIVATE_MORE, row); }

void EpubReaderActivity::navigateToHref(const std::string& href, bool savePosition) {
  runNavigationPolicy(XP_UI_READER_NAV_OPERATION_HREF, savePosition, 0, 0, &href);
}

void EpubReaderActivity::saveLinkStack() const { runLinkPersistence(0); }

void EpubReaderActivity::loadLinkStack() { runLinkPersistence(1); }

void EpubReaderActivity::runLinkPersistence(uint8_t load) const {
  struct Context {
    EpubReaderActivity& o;
    std::optional<std::string> path;
    std::optional<HalFile> file;
    uint32_t query(uint8_t query, uint32_t index) {
      switch (static_cast<XpUiReaderBookmarkLinkQuery>(query)) {
        case XP_UI_READER_BOOKMARK_LINK_QUERY_DEPTH: {
          ModelLock lock(o);
          return o.footnoteDepth;
        }
        case XP_UI_READER_BOOKMARK_LINK_QUERY_SPINE: {
          assert(index < MAX_FOOTNOTE_DEPTH);
          ModelLock lock(o);
          return o.savedPositions[index].spineIndex;
        }
        case XP_UI_READER_BOOKMARK_LINK_QUERY_PAGE: {
          assert(index < MAX_FOOTNOTE_DEPTH);
          ModelLock lock(o);
          return o.savedPositions[index].pageNumber;
        }
        case XP_UI_READER_BOOKMARK_LINK_QUERY_SPINE_COUNT:
          return o.epub->getSpineItemsCount();
      }
      return 0;
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderBookmarkLinkCommand>(command)) {
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_PATH_BEGIN:
          path.emplace(o.epub->getCachePath() + "/links.bin");
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_PATH_END:
          path.reset();
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_EXISTS:
          return Storage.exists(path->c_str());
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_FILE_BEGIN:
          file.emplace();
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_FILE_END:
          file.reset();
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_OPEN_READ:
          return Storage.openFileForRead("ERS", *path, *file);
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_OPEN_WRITE:
          return Storage.openFileForWrite("ERS", o.epub->getCachePath() + "/links.bin", *file);
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_REMOVE:
          Storage.remove(path->c_str());
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_LOG_WRITE_FAILED:
          LOG_ERR("ERS", "Failed to write link stack");
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_LOG_LOADED:
          LOG_DBG("ERS", "Loaded link stack, depth %d", static_cast<int>(a));
          break;
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_SET_DEPTH: {
          ModelLock lock(o);
          o.footnoteDepth = a;
          break;
        }
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_SET_SPINE: {
          assert(a >= 0 && a < MAX_FOOTNOTE_DEPTH);
          ModelLock lock(o);
          o.savedPositions[a].spineIndex = b;
          break;
        }
        case XP_UI_READER_BOOKMARK_LINK_COMMAND_SET_PAGE: {
          assert(a >= 0 && a < MAX_FOOTNOTE_DEPTH);
          ModelLock lock(o);
          o.savedPositions[a].pageNumber = b;
          break;
        }
      }
      return 0;
    }
  } context{const_cast<EpubReaderActivity&>(*this), {}, {}};
  const XpUiReaderLinkServices services{
      &context,
      [](void* c, uint8_t query, uint32_t index) -> uint32_t { return static_cast<Context*>(c)->query(query, index); },
      [](void* c, uint8_t command, int32_t a, int32_t b) -> uint32_t {
        return static_cast<Context*>(c)->command(command, a, b);
      },
      [](void* c, uint8_t write, uint8_t* bytes, uint32_t size) -> int32_t {
        auto& file = *static_cast<Context*>(c)->file;
        return write ? file.write(bytes, size) : file.read(bytes, size);
      }};
  xp_ui_reader_link_persistence(&services, load);
}

void EpubReaderActivity::restoreSavedPosition() { runNavigationPolicy(XP_UI_READER_NAV_OPERATION_RESTORE); }

namespace {
struct ReaderBookmarkResources {
  std::optional<RenderLock> lock;
  std::optional<SavedProgressPosition> progress;
  std::optional<std::string> text;
  std::optional<BookmarkEntry> entry;
  std::optional<uint32_t> offset;
};
}  // namespace

void EpubReaderActivity::loadCachedBookmarks() { runBookmarkPolicy(XP_UI_READER_BOOKMARK_OPERATION_LOAD); }

void EpubReaderActivity::addBookmark() {
  ReaderBookmarkResources resources;
  runBookmarkPolicy(XP_UI_READER_BOOKMARK_OPERATION_ADD, &resources);
}

void EpubReaderActivity::updateBookmarkFlag() { runBookmarkPolicy(XP_UI_READER_BOOKMARK_OPERATION_FLAG); }

void EpubReaderActivity::runBookmarkPolicy(uint8_t operation, void* resources) {
  struct Context {
    EpubReaderActivity& o;
    ReaderBookmarkResources* resources;
    ProgressRange range{};
    uint32_t read(uint8_t field) {
      switch (static_cast<XpUiReaderBookmarkField>(field)) {
        case XP_UI_READER_BOOKMARK_FIELD_HAS_SECTION:
          return bool(o.section);
        case XP_UI_READER_BOOKMARK_FIELD_HAS_BOOK:
          return bool(o.epub);
        case XP_UI_READER_BOOKMARK_FIELD_SPINE: {
          ModelLock lock(o);
          return o.currentSpineIndex;
        }
        case XP_UI_READER_BOOKMARK_FIELD_PAGE:
          return o.section->currentPage;
        case XP_UI_READER_BOOKMARK_FIELD_SECTION_COUNT:
          return o.section->pageCount;
        case XP_UI_READER_BOOKMARK_FIELD_BOOKMARK_COUNT:
          return o.cachedBookmarks.size();
        case XP_UI_READER_BOOKMARK_FIELD_HAS_CURRENT_OFFSET: {
          ModelLock lock(o);
          return o.currentPageVisibleOffset.has_value();
        }
        case XP_UI_READER_BOOKMARK_FIELD_CURRENT_OFFSET: {
          ModelLock lock(o);
          return *o.currentPageVisibleOffset;
        }
        case XP_UI_READER_BOOKMARK_FIELD_LOADED_OFFSET:
          return *resources->offset;
        case XP_UI_READER_BOOKMARK_FIELD_REMOVED: {
          ModelLock lock(o);
          return o.bookmarkRemoved;
        }
        case XP_UI_READER_BOOKMARK_FIELD_BOOKMARKED: {
          ModelLock lock(o);
          return o.currentPageBookmarked;
        }
      }
      return 0;
    }
    void write(uint8_t field, uint32_t value) {
      ModelLock lock(o);
      switch (static_cast<XpUiReaderBookmarkField>(field)) {
        case XP_UI_READER_BOOKMARK_FIELD_REMOVED:
          o.bookmarkRemoved = value != 0;
          break;
        case XP_UI_READER_BOOKMARK_FIELD_BOOKMARKED:
          o.currentPageBookmarked = value != 0;
          break;
        default:
          break;
      }
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderBookmarkCommand>(command)) {
        case XP_UI_READER_BOOKMARK_COMMAND_LOCK:
          resources->lock.emplace();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_UNLOCK:
          resources->lock.reset();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_LOG_TOGGLE:
          LOG_DBG("ERS", "Toggle bookmark at spine %d, page %d", o.currentSpineIndex,
                  o.section ? o.section->currentPage : -1);
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_ESTIMATE:
          return o.section->estimatedTotalPages();
        case XP_UI_READER_BOOKMARK_COMMAND_PROGRESS_BEGIN:
          resources->progress.emplace(ProgressMapper::toSavedProgress(o.epub, o.getCurrentPosition()));
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_PROGRESS_END:
          resources->progress.reset();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_RANGE:
          range = getPageProgressRange(o.epub, o.currentSpineIndex, a, b);
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_ERASE_MATCHES: {
          const size_t before = o.cachedBookmarks.size();
          o.cachedBookmarks.erase(std::remove_if(o.cachedBookmarks.begin(), o.cachedBookmarks.end(),
                                                 [&](const BookmarkEntry& entry) {
                                                   return bookmarkMatchesProgress(entry, o.currentSpineIndex, a, b,
                                                                                  range);
                                                 }),
                                  o.cachedBookmarks.end());
          return o.cachedBookmarks.size() != before;
        }
        case XP_UI_READER_BOOKMARK_COMMAND_ANY_MATCH:
          return std::any_of(o.cachedBookmarks.begin(), o.cachedBookmarks.end(), [&](const BookmarkEntry& entry) {
            return bookmarkMatchesProgress(entry, o.currentSpineIndex, a, b, range);
          });
        case XP_UI_READER_BOOKMARK_COMMAND_TEXT_BEGIN:
          resources->text.emplace();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_TEXT_READ:
          *resources->text = o.section->getTextFromSectionFile();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_TEXT_END:
          resources->text.reset();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_ENTRY_BEGIN: {
          auto& entry = resources->entry.emplace();
          entry.percentage = resources->progress->percentage;
          entry.xpath = resources->progress->xpath;
          entry.summary = BookmarkUtil::sanitizeBookmarkSummary(*resources->text);
          entry.computedSpineIndex = o.currentSpineIndex;
          entry.computedChapterPageCount = b;
          entry.computedChapterProgress = a;
          break;
        }
        case XP_UI_READER_BOOKMARK_COMMAND_ENTRY_OFFSET:
          resources->entry->visibleTextOffset = static_cast<uint32_t>(a);
          resources->entry->hasVisibleTextOffset = true;
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_ENTRY_INSERT:
          o.cachedBookmarks.insert(o.cachedBookmarks.begin(), *resources->entry);
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_ENTRY_END:
          resources->entry.reset();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_OFFSET_READ:
          resources->offset = o.section->getVisibleTextOffsetForPage(static_cast<uint16_t>(a));
          return resources->offset.has_value();
        case XP_UI_READER_BOOKMARK_COMMAND_CLEAR:
          o.cachedBookmarks.clear();
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_LOAD:
          BookmarkFile::load(o.epub->getPath(), o.cachedBookmarks);
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_SAVE:
          return BookmarkFile::save(o.epub->getPath(), o.cachedBookmarks);
        case XP_UI_READER_BOOKMARK_COMMAND_LOG_SAVE_FAILED:
          LOG_ERR("ERS", "Failed to save bookmarks");
          break;
        case XP_UI_READER_BOOKMARK_COMMAND_UPDATE:
          o.requestUpdate();
          break;
      }
      return 0;
    }
  } context{*this, static_cast<ReaderBookmarkResources*>(resources)};
  const XpUiReaderBookmarkServices services{
      &context, [](void* c, uint8_t field) -> uint32_t { return static_cast<Context*>(c)->read(field); },
      [](void* c, uint8_t field, uint32_t value) { static_cast<Context*>(c)->write(field, value); },
      [](void* c, uint8_t command, int32_t a, int32_t b) -> uint32_t {
        return static_cast<Context*>(c)->command(command, a, b);
      }};
  xp_ui_reader_bookmark(&services, operation);
}

ScreenshotInfo EpubReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo result;
  result.readerType = ScreenshotInfo::ReaderType::Epub;
  runProjection(XP_UI_READER_PROJECTION_OPERATION_SCREENSHOT, 0, 0, 0, &result);
  return result;
}

int EpubReaderActivity::getProgressBasisPoints() const {
  const uint32_t value = runProjection(XP_UI_READER_PROJECTION_OPERATION_BASIS_POINTS);
  return value == UINT32_MAX ? getProgressPercent() * 100 : static_cast<int>(value);
}

CrossPointPosition EpubReaderActivity::getCurrentPosition() const {
  CrossPointPosition result{};
  runProjection(XP_UI_READER_PROJECTION_OPERATION_POSITION, 0, 0, 0, &result);
  return result;
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

std::string EpubReaderActivity::readerViewValue(uint8_t operation, int32_t row) const {
  static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN) == 7);
  static_assert(static_cast<uint8_t>(EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN) == 8);
  static_assert(CrossPointSettings::FONT_FAMILY_COUNT == 1);
  struct Context {
    const EpubReaderActivity& o;
    std::string value;
    decltype(SETTINGS.statusBarSpec()) spec{};
    uint32_t query(uint8_t query, uint32_t argument) {
      switch (static_cast<XpUiReaderValueQuery>(query)) {
        case XP_UI_READER_VALUE_QUERY_HAS_SECTION: {
          return bool(o.section);
        }
        case XP_UI_READER_VALUE_QUERY_PAGE: {
          return o.section->currentPage;
        }
        case XP_UI_READER_VALUE_QUERY_ESTIMATED_COUNT: {
          return o.section->estimatedTotalPages();
        }
        case XP_UI_READER_VALUE_QUERY_HAS_BOOK: {
          return bool(o.epub);
        }
        case XP_UI_READER_VALUE_QUERY_PROGRESS: {
          float fraction;
          memcpy(&fraction, &argument, sizeof(fraction));
          const float progress = o.epub->calculateProgress(o.currentSpineIndex, fraction);
          uint32_t bits;
          memcpy(&bits, &progress, sizeof(bits));
          return bits;
        }
        case XP_UI_READER_VALUE_QUERY_TITLE_MODE: {
          return spec.titleMode;
        }
        case XP_UI_READER_VALUE_QUERY_AUTOMATIC: {
          return o.automaticPageTurnActive;
        }
        case XP_UI_READER_VALUE_QUERY_TURN_DURATION: {
          return o.pageTurnDuration;
        }
        case XP_UI_READER_VALUE_QUERY_STATUS_HEIGHT: {
          return UITheme::getInstance().getStatusBarHeight();
        }
        case XP_UI_READER_VALUE_QUERY_PROGRESS_HEIGHT: {
          return UITheme::getInstance().getProgressBarHeight();
        }
        case XP_UI_READER_VALUE_QUERY_STATUS_MARGIN: {
          return UITheme::getInstance().getMetrics().statusBarVerticalMargin;
        }
        case XP_UI_READER_VALUE_QUERY_TOC_INDEX: {
          return o.epub->getTocIndexForSpineIndex(o.currentSpineIndex);
        }
        case XP_UI_READER_VALUE_QUERY_BOOKMARKED: {
          return o.currentPageBookmarked;
        }
        case XP_UI_READER_VALUE_QUERY_BUILDING: {
          return o.section->isBuilding();
        }
        case XP_UI_READER_VALUE_QUERY_HAS_SD_FONT: {
          return SETTINGS.sdFontFamilyName[0] != '\0';
        }
        case XP_UI_READER_VALUE_QUERY_POINT_SIZE: {
          return SETTINGS.fontPointSize;
        }
        case XP_UI_READER_VALUE_QUERY_SPACING: {
          return SETTINGS.lineSpacing;
        }
        case XP_UI_READER_VALUE_QUERY_ALIGNMENT: {
          return SETTINGS.paragraphAlignment;
        }
        case XP_UI_READER_VALUE_QUERY_SPACING_LABEL: {
          assert(argument < std::size(kSpacingIds));
          return static_cast<uint32_t>(kSpacingIds[argument]);
        }
        case XP_UI_READER_VALUE_QUERY_ALIGNMENT_LABEL: {
          assert(argument < std::size(kAlignIds));
          return static_cast<uint32_t>(kAlignIds[argument]);
        }
        case XP_UI_READER_VALUE_QUERY_MORE_COUNT: {
          return o.moreItems.size();
        }
        case XP_UI_READER_VALUE_QUERY_MORE_ACTION: {
          assert(argument < o.moreItems.size());
          return static_cast<uint32_t>(o.moreItems[argument].action);
        }
        case XP_UI_READER_VALUE_QUERY_MORE_LABEL: {
          assert(argument < o.moreItems.size());
          return static_cast<uint32_t>(o.moreItems[argument].labelId);
        }
        case XP_UI_READER_VALUE_QUERY_ORIENTATION: {
          return SETTINGS.orientation;
        }
        case XP_UI_READER_VALUE_QUERY_ORIENTATION_LABEL: {
          static constexpr StrId labels[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW,
                                             StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};
          static_assert(std::size(labels) == CrossPointSettings::ORIENTATION_COUNT);
          assert(argument < std::size(labels));
          return static_cast<uint32_t>(labels[argument]);
        }
        case XP_UI_READER_VALUE_QUERY_AUTO_OPTION: {
          return o.autoTurnOption;
        }
        case XP_UI_READER_VALUE_QUERY_TEXT_NAME: {
          assert(argument < std::size(kTextRowNames));
          return static_cast<uint32_t>(kTextRowNames[argument]);
        }
      }
      return 0;
    }
    uint32_t command(uint8_t command, uint32_t argument) {
      switch (static_cast<XpUiReaderValueCommand>(command)) {
        case XP_UI_READER_VALUE_COMMAND_LABEL: {
          value = I18N.get(static_cast<StrId>(argument));
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_UNNAMED: {
          value = tr(STR_UNNAMED);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_CHAPTER_COPY: {
          const auto item = o.epub->getTocItem(static_cast<int32_t>(argument));
          value = item.title;
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_CHAPTER: {
          value = o.epub->getTocItem(static_cast<int32_t>(argument)).title;
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_SAMPLE_SPEC: {
          spec = SETTINGS.statusBarSpec();
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_TAKE_LINK_NOTE: {
          return static_cast<uint32_t>(o.linkNoteTitle.draw(argument != 0, bleturner::linkNote()));
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_CONNECTED: {
          const auto peer = bleturner::linked();
          char connected[64];
          snprintf(connected, sizeof(connected), tr(STR_BT_CONNECTED_TO), peer.name[0] != '\0' ? peer.name : peer.addr);
          value = connected;
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_LOW_MEMORY: {
          value = tr(STR_BT_LOW_MEMORY);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_CONNECTING: {
          value = tr(STR_BT_CONNECTING);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_FAILED: {
          value = tr(STR_BT_FAILED);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_AUTOMATIC: {
          value = tr(STR_AUTO_TURN_ENABLED) + std::to_string(argument);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_BOOK: {
          value = o.epub->getTitle();
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_SD_FONT: {
          value = SETTINGS.sdFontFamilyName;
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_NOTO_SANS: {
          value = I18N.get(StrId::STR_NOTO_SANS);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_POINT_SIZE: {
          value = std::to_string(argument) + " pt";
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_OFF: {
          value = tr(STR_STATE_OFF);
          break;
        }
        case XP_UI_READER_VALUE_COMMAND_NUMBER: {
          value = std::to_string(argument);
          break;
        }
      }
      return 0;
    }
  } context{*this, {}, {}};
  const XpUiReaderValueServices services{
      &context, [](void* p, uint8_t q, uint32_t a) { return static_cast<Context*>(p)->query(q, a); },
      [](void* p, uint8_t c, uint32_t a) { return static_cast<Context*>(p)->command(c, a); },
      [](void* p, const XpUiReaderStatusValue* v) {
        auto& c = *static_cast<Context*>(p);
        GUI.drawStatusBar(c.o.renderer, v->book_progress, v->page, v->page_count, c.value, 0, v->y_offset, true,
                          v->bookmarked, v->building);
      }};
  xp_ui_reader_view_value(&services, operation, row);
  return std::move(context.value);
}

bool EpubReaderActivity::runBookLifetime(uint8_t operation) {
  struct MovePaths {
    std::string source, cache, destination;
  };
  struct Context {
    EpubReaderActivity& o;
    std::unique_ptr<Epub> loaded;
    std::optional<GfxRenderer::FrameBufferLoan> loan;
    std::optional<HalFile> file;
    uint8_t progress[10]{};
    std::optional<MovePaths> paths;
    uint32_t read(uint8_t field) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderLifetimeField>(field)) {
        case XP_UI_READER_LIFETIME_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_LIFETIME_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_LIFETIME_FIELD_CACHED_SPINE:
          return static_cast<uint32_t>(o.cachedSpineIndex);
        case XP_UI_READER_LIFETIME_FIELD_CACHED_COUNT:
          return static_cast<uint32_t>(o.cachedChapterTotalPageCount);
        case XP_UI_READER_LIFETIME_FIELD_HAS_CACHED_OFFSET:
          return static_cast<uint32_t>(o.cachedVisibleTextOffset.has_value());
        case XP_UI_READER_LIFETIME_FIELD_CACHED_OFFSET:
          return static_cast<uint32_t>(o.cachedVisibleTextOffset.value_or(0));
        case XP_UI_READER_LIFETIME_FIELD_RADIO_RELEASED:
          return static_cast<uint32_t>(o.radioReleasedForBuild.load());
        case XP_UI_READER_LIFETIME_FIELD_FOOTNOTE_DEPTH:
          return static_cast<uint32_t>(o.footnoteDepth);
        case XP_UI_READER_LIFETIME_FIELD_PENDING_READ_MOVE:
          return static_cast<uint32_t>(o.pendingReadFolderMove);
      }
      return 0;
    }
    void write(uint8_t field, uint32_t value) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderLifetimeField>(field)) {
        case XP_UI_READER_LIFETIME_FIELD_SPINE: {
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_NEXT_PAGE: {
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_CACHED_SPINE: {
          o.cachedSpineIndex = static_cast<decltype(o.cachedSpineIndex)>(value);
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_CACHED_COUNT: {
          o.cachedChapterTotalPageCount = static_cast<decltype(o.cachedChapterTotalPageCount)>(value);
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_HAS_CACHED_OFFSET: {
          assert(!value);
          o.cachedVisibleTextOffset.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_CACHED_OFFSET: {
          o.cachedVisibleTextOffset = value;
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_RADIO_RELEASED: {
          o.radioReleasedForBuild = value != 0;
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_FOOTNOTE_DEPTH: {
          o.footnoteDepth = static_cast<decltype(o.footnoteDepth)>(value);
          break;
        }
        case XP_UI_READER_LIFETIME_FIELD_PENDING_READ_MOVE: {
          o.pendingReadFolderMove = static_cast<decltype(o.pendingReadFolderMove)>(value);
          break;
        }
      }
    }
    uint32_t query(uint8_t query, uint32_t argument) {
      switch (static_cast<XpUiReaderLifetimeQuery>(query)) {
        case XP_UI_READER_LIFETIME_QUERY_CACHE_PRESENT: {
          return Storage.exists((loaded->getCachePath() + "/book.bin").c_str());
        }
        case XP_UI_READER_LIFETIME_QUERY_EMBEDDED_STYLE: {
          return SETTINGS.embeddedStyle;
        }
        case XP_UI_READER_LIFETIME_QUERY_PROGRESS_BYTE: {
          assert(argument < sizeof(progress));
          return progress[argument];
        }
        case XP_UI_READER_LIFETIME_QUERY_TEXT_REFERENCE: {
          return o.epub->getSpineIndexForTextReference();
        }
        case XP_UI_READER_LIFETIME_QUERY_HAS_BOOK: {
          return bool(o.epub);
        }
      }
      return 0;
    }
    uint32_t command(uint8_t command, int32_t a, int32_t) {
      switch (static_cast<XpUiReaderLifetimeCommand>(command)) {
        case XP_UI_READER_LIFETIME_COMMAND_NEW_LOADED: {
          assert(!loaded);
          loaded = makeUniqueNoThrow<Epub>(o.bookPath, "/.crosspoint");
          return bool(loaded);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DROP_LOADED: {
          loaded.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOG_ALLOCATION_FAILED: {
          LOG_ERR("ERS", "Failed to allocate EPUB object");
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DISABLE_FAST: {
          o.disableFastInitialRefresh();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_POPUP_INDEX: {
          GUI.drawPopup(o.renderer, tr(STR_INDEXING));
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOAN_BEGIN: {
          assert(!loan);
          loan.emplace(o.renderer);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOAN_DROP: {
          loan.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOAD: {
          return loaded->load(true, a != 0);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_PROTECTION_ERROR: {
          o.loadProtectionError = loaded->getProtectionError();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOG_LOAD_FAILED: {
          LOG_ERR("ERS", "Failed to load EPUB%s%s", o.loadProtectionError.empty() ? "" : ": ",
                  o.loadProtectionError.c_str());
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_MOVE_LOADED: {
          o.epub = std::move(loaded);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_CLEAR_IMAGE_FAILURES: {
          ImageBlock::clearRenderFailures();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_INSTALL_EXTRACTOR: {
          ImageBlock::setExtractor(o.epub.get(), [](void* ctx, const char* src, const char* dest) {
            return static_cast<Epub*>(ctx)->extractItemToFile(src, dest);
          });
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_SETUP_CACHE: {
          o.epub->setupCacheDir();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_OPEN_PROGRESS: {
          assert(!file);
          file.emplace();
          return Storage.openFileForRead("ERS", o.epub->getCachePath() + "/progress.bin", *file);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DROP_FILE: {
          file.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_READ_PROGRESS: {
          assert(file);
          return file->read(progress, sizeof(progress));
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOG_SENTINEL: {
          LOG_DBG("ERS", "Ignoring stale last-page sentinel from progress cache");
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOG_PROGRESS: {
          LOG_DBG("ERS", "Loaded cache: %d, %d", o.currentSpineIndex, o.nextPageNumber);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOG_TEXT_REFERENCE: {
          LOG_DBG("ERS", "Opened for first time, navigating to text reference at index %d", a);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOAD_LINKS: {
          o.loadLinkStack();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOAD_BOOKMARKS: {
          o.loadCachedBookmarks();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOAD_CLIPPINGS: {
          return CLIPPINGS.loadForBook(o.epub->getPath(), o.epub->getTitle(), o.epub->getAuthor(), "epub");
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_LOG_CLIPPINGS_FAILED: {
          LOG_ERR("CLIP", "Failed to load clippings for %s", o.epub->getPath().c_str());
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_REMOVE_EXTRACTOR: {
          ImageBlock::setExtractor(nullptr, nullptr);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_SETTLE_OVERLAY: {
          o.settleOverlayRefresh();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DISCARD_OVERLAY: {
          o.discardOverlayPage();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_RADIO_AFTER: {
          bleturner::afterPaint();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_SAVE_LINKS: {
          o.saveLinkStack();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DROP_SECTION: {
          o.section.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_UNLOAD_CLIPPINGS: {
          CLIPPINGS.unload();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_PREPARE_MOVE: {
          assert(!paths);
          paths.emplace();
          paths->source = o.epub->getPath();
          paths->cache = o.epub->getCachePath();
          paths->destination = buildReadFolderDestination(paths->source);
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DROP_MOVE_PATHS: {
          paths.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_DROP_BOOK: {
          o.epub.reset();
          break;
        }
        case XP_UI_READER_LIFETIME_COMMAND_MOVE_BOOK: {
          assert(paths);
          moveFinishedBookToReadFolder(paths->source, paths->destination, paths->cache);
          break;
        }
      }
      return 0;
    }
  } context{*this, {}, {}, {}, {}, {}};
  const XpUiReaderLifetimeServices services{
      &context, [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t q, uint32_t a) { return static_cast<Context*>(p)->query(q, a); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); }};
  const bool result = xp_ui_reader_book_lifetime(&services, operation);
  assert(!context.loaded && !context.loan && !context.file && !context.paths);
  return result;
}

bool EpubReaderActivity::runBuildPolicy(uint8_t operation) const {
  struct Context {
    EpubReaderActivity& o;
    std::optional<RenderLock> lock;
    uint32_t freeHeap = 0, maxBlock = 0;
    uint32_t read(uint8_t field) {
      switch (static_cast<XpUiReaderBuildField>(field)) {
        case XP_UI_READER_BUILD_FIELD_HAS_SECTION:
          return static_cast<uint32_t>(bool(o.section));
        case XP_UI_READER_BUILD_FIELD_BUILDING:
          return static_cast<uint32_t>(o.section->isBuilding());
        case XP_UI_READER_BUILD_FIELD_COMPLETE:
          return static_cast<uint32_t>(o.section->isBuildComplete());
        case XP_UI_READER_BUILD_FIELD_PAGE:
          return static_cast<uint32_t>(o.section->currentPage);
        case XP_UI_READER_BUILD_FIELD_FRAMEBUFFER:
          return static_cast<uint32_t>(o.renderer.hasFrameBuffer());
        default:
          break;
      }
      ModelLock guard(o);
      switch (static_cast<XpUiReaderBuildField>(field)) {
        case XP_UI_READER_BUILD_FIELD_RADIO_RELEASED:
          return static_cast<uint32_t>(o.radioReleasedForBuild.load());
        case XP_UI_READER_BUILD_FIELD_HEAP_PAUSED:
          return static_cast<uint32_t>(o.buildHeapPaused);
        case XP_UI_READER_BUILD_FIELD_POPUP_PENDING:
          return static_cast<uint32_t>(o.buildPopupPending);
        case XP_UI_READER_BUILD_FIELD_REFRESH:
          return static_cast<uint32_t>(o.renderRefreshCounter());
        case XP_UI_READER_BUILD_FIELD_OVERLAY:
          return static_cast<uint32_t>(o.overlay);
        case XP_UI_READER_BUILD_FIELD_PENDING_TURN:
          return static_cast<uint32_t>(o.pendingManualTurn);
        case XP_UI_READER_BUILD_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_BUILD_FIELD_FAILED_SPINE:
          return static_cast<uint32_t>(o.backgroundBuildFailedSpine);
        case XP_UI_READER_BUILD_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_BUILD_FIELD_BUFFER_STALE:
          return static_cast<uint32_t>(o.pageBufferStale.load());
        default:
          return 0;
      }
    }
    void write(uint8_t field, uint32_t value) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderBuildField>(field)) {
        case XP_UI_READER_BUILD_FIELD_RADIO_RELEASED: {
          o.radioReleasedForBuild = value != 0;
          break;
        }
        case XP_UI_READER_BUILD_FIELD_HEAP_PAUSED: {
          o.buildHeapPaused = static_cast<decltype(o.buildHeapPaused)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_POPUP_PENDING: {
          o.buildPopupPending = static_cast<decltype(o.buildPopupPending)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_REFRESH: {
          o.renderRefreshCounter() = static_cast<int32_t>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_OVERLAY: {
          o.overlay = static_cast<decltype(o.overlay)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_PENDING_TURN: {
          o.pendingManualTurn = static_cast<decltype(o.pendingManualTurn)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_SPINE: {
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_FAILED_SPINE: {
          o.backgroundBuildFailedSpine = static_cast<decltype(o.backgroundBuildFailedSpine)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_NEXT_PAGE: {
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        }
        case XP_UI_READER_BUILD_FIELD_BUFFER_STALE: {
          o.pageBufferStale = value != 0;
          break;
        }
        default:
          break;
      }
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderBuildCommand>(command)) {
        case XP_UI_READER_BUILD_COMMAND_RELEASE_STRIPS: {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          return o.stripBuffers.releaseIdle();
#else
          return false;
#endif
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_RADIO_STATUS: {
          const auto status = bleturner::status();
          return status.running | (status.stopping << 1) | (status.starting << 2);
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_RADIO_BEFORE: {
          return bleturner::beforeChapterBuild() != bleturner::BuildRelease::NotHeld;
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_CLOCK: {
          return static_cast<uint32_t>(millis());
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_SLEEP: {
          delay(static_cast<uint32_t>(a));
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_LOG_ROOM_DENIED: {
          LOG_ERR("ERS", "Bluetooth radio still holds the heap after %u ms; chapter build refused",
                  static_cast<unsigned>(a));
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_READ_FREE: {
          freeHeap = ESP.getFreeHeap();
          return freeHeap;
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_READ_BLOCK: {
          maxBlock = ESP.getMaxAllocHeap();
          return maxBlock;
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_LOG_RECLAIMED: {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          LOG_DBG("ERS", "Released idle reader strips for build: heap %u/%u -> %u/%u", static_cast<unsigned>(a),
                  static_cast<unsigned>(b), static_cast<unsigned>(freeHeap), static_cast<unsigned>(maxBlock));
#endif
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_PAINT_INDEX: {
          GUI.drawPopup(o.renderer, tr(STR_INDEXING));
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_TRY_LOCK: {
          assert(!lock);
          lock.emplace(RenderLock::Mode::Try);
          return lock->ownsLock();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_UNLOCK: {
          lock.reset();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_BUTTON: {
          using Button = MappedInputManager::Button;
          constexpr Button buttons[] = {Button::Back, Button::Confirm, Button::Left, Button::Right,
                                        Button::Up,   Button::Down,    Button::Power};
          assert(a >= 0 && a < 7);
          return o.mappedInput.isPressed(buttons[a]);
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_RAW: {
          return buttonContactClosed();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_LOAN_COUNT: {
          return o.renderer.frameBufferLoanCount();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_BUILD_SOME: {
          return o.section->buildSomeMore(BACKGROUND_BUILD_PAGES_PER_TICK, &buttonContactClosed);
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_LOG_BUILD_FAILED: {
          LOG_ERR("ERS", "Background section build failed");
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_DROP_SECTION: {
          o.section.reset();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_REPOSITION: {
          return o.applyDeferredReposition();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_UPDATE: {
          o.requestUpdate();
          break;
        }
        case XP_UI_READER_BUILD_COMMAND_RADIO_HEAP: {
          return bleturner::holdsHeap();
          break;
        }
      }
      return 0;
    }
  } context{const_cast<EpubReaderActivity&>(*this), {}};
  const XpUiReaderBuildServices services{
      &context, [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); }};
  const bool result = xp_ui_reader_build_policy(&services, operation);
  assert(!context.lock);
  return result;
}

void EpubReaderActivity::runSettingsPolicy(uint8_t operation, int32_t argument,
                                           const std::vector<uint8_t>* resultSizes) {
  struct Choices {
    const std::vector<uint8_t> sizes;
    std::vector<std::string> labels;
    Choices() : sizes(readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName)) {}
  };
  struct Context {
    EpubReaderActivity& o;
    const std::vector<uint8_t>* resultSizes;
    std::optional<RenderLock> lock;
    std::optional<Choices> choices;
    uint32_t read(uint8_t field) {
      switch (static_cast<XpUiReaderSettingsField>(field)) {
        case XP_UI_READER_SETTINGS_FIELD_ORIENTATION:
          return SETTINGS.orientation;
        case XP_UI_READER_SETTINGS_FIELD_FONT_SIZE:
          return SETTINGS.fontPointSize;
        case XP_UI_READER_SETTINGS_FIELD_SPACING:
          return SETTINGS.lineSpacing;
        case XP_UI_READER_SETTINGS_FIELD_ALIGNMENT:
          return SETTINGS.paragraphAlignment;
        case XP_UI_READER_SETTINGS_FIELD_HAS_SECTION:
          return static_cast<uint32_t>(bool(o.section));
        case XP_UI_READER_SETTINGS_FIELD_PAGE_COUNT:
          return static_cast<uint32_t>(o.section->pageCount);
        case XP_UI_READER_SETTINGS_FIELD_PAGE:
          return static_cast<uint32_t>(o.section->currentPage);
        case XP_UI_READER_SETTINGS_FIELD_RATE_COUNT:
          return static_cast<uint32_t>(std::size(PAGE_TURN_RATES));
        case XP_UI_READER_SETTINGS_FIELD_STATUS_HEIGHT:
          return static_cast<uint32_t>(UITheme::getInstance().getStatusBarHeight());
        case XP_UI_READER_SETTINGS_FIELD_PROGRESS_HEIGHT:
          return static_cast<uint32_t>(UITheme::getInstance().getProgressBarHeight());
        case XP_UI_READER_SETTINGS_FIELD_SIZE_COUNT:
          return static_cast<uint32_t>(resultSizes ? resultSizes->size() : (choices ? choices->sizes.size() : 0));
        case XP_UI_READER_SETTINGS_FIELD_SPACING_COUNT:
          return static_cast<uint32_t>(CrossPointSettings::LINE_COMPRESSION_COUNT);
        case XP_UI_READER_SETTINGS_FIELD_ALIGNMENT_COUNT:
          return static_cast<uint32_t>(CrossPointSettings::PARAGRAPH_ALIGNMENT_COUNT);
        default:
          break;
      }
      ModelLock guard(o);
      switch (static_cast<XpUiReaderSettingsField>(field)) {
        case XP_UI_READER_SETTINGS_FIELD_APPLIED_ORIENTATION:
          return static_cast<uint32_t>(o.appliedOrientation);
        case XP_UI_READER_SETTINGS_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_SETTINGS_FIELD_CACHED_SPINE:
          return static_cast<uint32_t>(o.cachedSpineIndex);
        case XP_UI_READER_SETTINGS_FIELD_CACHED_COUNT:
          return static_cast<uint32_t>(o.cachedChapterTotalPageCount);
        case XP_UI_READER_SETTINGS_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_SETTINGS_FIELD_LAST_TURN:
          return static_cast<uint32_t>(o.lastPageTurnTime);
        case XP_UI_READER_SETTINGS_FIELD_TURN_DURATION:
          return static_cast<uint32_t>(o.pageTurnDuration);
        case XP_UI_READER_SETTINGS_FIELD_AUTO_ACTIVE:
          return static_cast<uint32_t>(o.automaticPageTurnActive);
        default:
          return 0;
      }
    }
    void write(uint8_t field, uint32_t value) {
      switch (static_cast<XpUiReaderSettingsField>(field)) {
        case XP_UI_READER_SETTINGS_FIELD_ORIENTATION:
          SETTINGS.orientation = static_cast<uint8_t>(value);
          return;
        case XP_UI_READER_SETTINGS_FIELD_FONT_SIZE:
          SETTINGS.fontPointSize = static_cast<uint8_t>(value);
          return;
        case XP_UI_READER_SETTINGS_FIELD_SPACING:
          SETTINGS.lineSpacing = static_cast<uint8_t>(value);
          return;
        case XP_UI_READER_SETTINGS_FIELD_ALIGNMENT:
          SETTINGS.paragraphAlignment = static_cast<uint8_t>(value);
          return;
        default:
          break;
      }
      ModelLock guard(o);
      switch (static_cast<XpUiReaderSettingsField>(field)) {
        case XP_UI_READER_SETTINGS_FIELD_APPLIED_ORIENTATION:
          o.appliedOrientation = static_cast<decltype(o.appliedOrientation)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_SPINE:
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_CACHED_SPINE:
          o.cachedSpineIndex = static_cast<decltype(o.cachedSpineIndex)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_CACHED_COUNT:
          o.cachedChapterTotalPageCount = static_cast<decltype(o.cachedChapterTotalPageCount)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_NEXT_PAGE:
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_LAST_TURN:
          o.lastPageTurnTime = static_cast<decltype(o.lastPageTurnTime)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_TURN_DURATION:
          o.pageTurnDuration = static_cast<decltype(o.pageTurnDuration)>(value);
          break;
        case XP_UI_READER_SETTINGS_FIELD_AUTO_ACTIVE:
          o.automaticPageTurnActive = static_cast<decltype(o.automaticPageTurnActive)>(value);
          break;
        default:
          break;
      }
    }
    uint32_t command(uint8_t command, int32_t a, int32_t) {
      switch (static_cast<XpUiReaderSettingsCommand>(command)) {
        case XP_UI_READER_SETTINGS_COMMAND_BASE_ORIENTATION: {
          o.ReaderActivity::applyInitialOrientation();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_LOCK: {
          assert(!lock);
          if (a)
            lock.emplace(o);
          else
            lock.emplace();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_UNLOCK: {
          lock.reset();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_REMEMBER_OFFSET: {
          o.rememberCurrentContentOffset();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_DROP_SECTION: {
          o.section.reset();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_SAVE_SETTINGS: {
          SETTINGS.saveToFile();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_APPLY_ORIENTATION: {
          ReaderUtils::applyOrientation(o.renderer, SETTINGS.orientation);
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_CLOCK: {
          return static_cast<uint32_t>(millis());
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_RATE: {
          assert(a >= 0 && static_cast<size_t>(a) < std::size(PAGE_TURN_RATES));
          return PAGE_TURN_RATES[a];
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_ENSURE_FONTS: {
          sdFontSystem.ensureLoaded(o.renderer);
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_DISCARD_OVERLAY: {
          o.discardOverlayPage();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_UPDATE: {
          o.requestUpdate();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_PREPARE_SIZES: {
          assert(!choices);
          choices.emplace();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_DROP_SIZES: {
          choices.reset();
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_RESERVE_LABELS: {
          assert(choices);
          choices->labels.reserve(static_cast<size_t>(a));
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_ADD_LABEL: {
          assert(choices && a >= 0 && static_cast<size_t>(a) < choices->sizes.size());
          choices->labels.push_back(std::to_string(choices->sizes[a]) + " pt");
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_SNAP_SIZE: {
          assert(choices);
          return snapToNearestPointSize(choices->sizes, SETTINGS.fontPointSize);
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_SIZE_AT: {
          const auto& sizes = resultSizes ? *resultSizes : choices->sizes;
          assert(a >= 0 && static_cast<size_t>(a) < sizes.size());
          return sizes[a];
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_SHOW_SIZES: {
          assert(choices);
          o.overlayPopup.show(StrId::STR_FONT_SIZE, choices->labels, a,
                              [owner = &o, sizes = choices->sizes](int index) {
                                owner->runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_SIZE_RESULT, index, &sizes);
                              });
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_SHOW_SPACING: {
          o.overlayPopup.show(StrId::STR_LINE_SPACING, kSpacingIds, static_cast<int>(std::size(kSpacingIds)), a,
                              [owner = &o](int index) {
                                owner->runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_SPACING_RESULT, index);
                              });
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_SHOW_ALIGNMENT: {
          o.overlayPopup.show(StrId::STR_PARA_ALIGNMENT, kAlignIds, static_cast<int>(std::size(kAlignIds)), a,
                              [owner = &o](int index) {
                                owner->runSettingsPolicy(XP_UI_READER_SETTINGS_OPERATION_ALIGNMENT_RESULT, index);
                              });
          break;
        }
        case XP_UI_READER_SETTINGS_COMMAND_PAINT_POPUP: {
          o.paintOverlayPopup();
          break;
        }
      }
      return 0;
    }
  } context{*this, resultSizes, {}, {}};
  const XpUiReaderSettingsServices services{
      &context, [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); }};
  xp_ui_reader_settings(&services, operation, argument);
  assert(!context.lock && !context.choices);
}

void EpubReaderActivity::runMenuPolicy(uint8_t operation, int32_t argument, const ActivityResult* result) {
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::SELECT_CHAPTER) ==
                XP_UI_READER_PARENT_ACTION_CHAPTER);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::FOOTNOTES) ==
                XP_UI_READER_PARENT_ACTION_FOOTNOTES);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS) == XP_UI_READER_PARENT_ACTION_TEXT);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::NIGHT_MODE) == XP_UI_READER_PARENT_ACTION_NIGHT);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::FRONTLIGHT) ==
                XP_UI_READER_PARENT_ACTION_FRONTLIGHT);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::BLUETOOTH) ==
                XP_UI_READER_PARENT_ACTION_BLUETOOTH);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::GO_TO_PERCENT) ==
                XP_UI_READER_PARENT_ACTION_PERCENT);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::AUTO_PAGE_TURN) ==
                XP_UI_READER_PARENT_ACTION_AUTO);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::ROTATE_SCREEN) ==
                XP_UI_READER_PARENT_ACTION_ORIENTATION);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::BOOKMARKS) ==
                XP_UI_READER_PARENT_ACTION_BOOKMARKS);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::SAVE_CLIPPING) ==
                XP_UI_READER_PARENT_ACTION_SAVE_CLIPPING);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::VIEW_CLIPPINGS) ==
                XP_UI_READER_PARENT_ACTION_VIEW_CLIPPINGS);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::TOGGLE_BOOKMARK) ==
                XP_UI_READER_PARENT_ACTION_TOGGLE_BOOKMARK);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::SCREENSHOT) ==
                XP_UI_READER_PARENT_ACTION_SCREENSHOT);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::DISPLAY_QR) == XP_UI_READER_PARENT_ACTION_QR);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::GO_HOME) == XP_UI_READER_PARENT_ACTION_HOME);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::SYNC) == XP_UI_READER_PARENT_ACTION_SYNC);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::DELETE_CACHE) ==
                XP_UI_READER_PARENT_ACTION_DELETE_CACHE);
  static_assert(static_cast<int>(EpubReaderMenuActivity::MenuAction::DICTIONARY) ==
                XP_UI_READER_PARENT_ACTION_DICTIONARY);
  struct Context {
    EpubReaderActivity& o;
    const ActivityResult* result;
    ChapterPosition position{};
    std::optional<RenderLock> lock;
    std::optional<std::string> qr;
    std::optional<std::vector<std::string>> labels;
    uint16_t temporaryCount = 0;
    uint32_t read(uint8_t field) {
      switch (static_cast<XpUiReaderParentField>(field)) {
        case XP_UI_READER_PARENT_FIELD_ORIENTATION:
          return SETTINGS.orientation;
        case XP_UI_READER_PARENT_FIELD_SCREEN_INVERTED:
          return SETTINGS.screenInverted;
        case XP_UI_READER_PARENT_FIELD_FRONTLIGHT_ON:
          return SETTINGS.frontlightOn;
        case XP_UI_READER_PARENT_FIELD_USES_TOOLBAR:
          return static_cast<uint32_t>(o.usesToolbarMenu());
        case XP_UI_READER_PARENT_FIELD_HAS_TOOLBAR:
          return static_cast<uint32_t>(bool(o.toolbarUi));
        case XP_UI_READER_PARENT_FIELD_TOUCH:
          return static_cast<uint32_t>(o.mappedInput.hasTouch());
        case XP_UI_READER_PARENT_FIELD_HAS_BOOK:
          return static_cast<uint32_t>(bool(o.epub));
        case XP_UI_READER_PARENT_FIELD_HAS_SECTION:
          return static_cast<uint32_t>(bool(o.section));
        case XP_UI_READER_PARENT_FIELD_PAGE:
          return static_cast<uint32_t>(o.section->currentPage);
        case XP_UI_READER_PARENT_FIELD_PAGE_COUNT:
          return static_cast<uint32_t>(o.section->pageCount);
        case XP_UI_READER_PARENT_FIELD_MORE_COUNT:
          return static_cast<uint32_t>(o.moreItems.size());
        case XP_UI_READER_PARENT_FIELD_ORIENTATION_COUNT:
          return static_cast<uint32_t>(CrossPointSettings::ORIENTATION_COUNT);
        case XP_UI_READER_PARENT_FIELD_RATE_COUNT:
          return static_cast<uint32_t>(std::size(PAGE_TURN_RATES));
        case XP_UI_READER_PARENT_FIELD_CANCELLED:
          return static_cast<uint32_t>(result->isCancelled);
        case XP_UI_READER_PARENT_FIELD_RESULT_ORIENTATION:
          return static_cast<uint32_t>(std::get<MenuResult>(result->data).orientation);
        case XP_UI_READER_PARENT_FIELD_RESULT_AUTO:
          return static_cast<uint32_t>(std::get<MenuResult>(result->data).pageTurnOption);
        case XP_UI_READER_PARENT_FIELD_RESULT_ACTION:
          return static_cast<uint32_t>(std::get<MenuResult>(result->data).action);
        case XP_UI_READER_PARENT_FIELD_RESULT_SPINE:
          return static_cast<uint32_t>(std::get<ChapterResult>(result->data).spineIndex);
        case XP_UI_READER_PARENT_FIELD_TEMPORARY_COUNT:
          return static_cast<uint32_t>(temporaryCount);
        case XP_UI_READER_PARENT_FIELD_STRIPS_ENABLED:
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          return true;
#else
          return false;
#endif
        default:
          break;
      }
      ModelLock guard(o);
      switch (static_cast<XpUiReaderParentField>(field)) {
        case XP_UI_READER_PARENT_FIELD_PENDING_TURN:
          return static_cast<uint32_t>(o.pendingManualTurn);
        case XP_UI_READER_PARENT_FIELD_OVERLAY:
          return static_cast<uint32_t>(o.overlay);
        case XP_UI_READER_PARENT_FIELD_FOCUS:
          return static_cast<uint32_t>(o.focusedTool);
        case XP_UI_READER_PARENT_FIELD_HOLD_JUMPED:
          return static_cast<uint32_t>(o.panelHoldJumped);
        case XP_UI_READER_PARENT_FIELD_CURSOR:
          return static_cast<uint32_t>(o.panelCursorShown);
        case XP_UI_READER_PARENT_FIELD_CACHED_SPINE:
          return static_cast<uint32_t>(o.cachedSpineIndex);
        case XP_UI_READER_PARENT_FIELD_CACHED_COUNT:
          return static_cast<uint32_t>(o.cachedChapterTotalPageCount);
        case XP_UI_READER_PARENT_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_PARENT_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_PARENT_FIELD_SCREENSHOT:
          return static_cast<uint32_t>(o.pendingScreenshot);
        case XP_UI_READER_PARENT_FIELD_OVERLAY_STORED:
          return static_cast<uint32_t>(o.overlayPageStored);
        case XP_UI_READER_PARENT_FIELD_SHOW_BOOKMARK:
          return static_cast<uint32_t>(o.showBookmarkMessage);
        case XP_UI_READER_PARENT_FIELD_BOOKMARK_TIME:
          return static_cast<uint32_t>(o.bookmarkMessageTime);
        case XP_UI_READER_PARENT_FIELD_AUTO_OPTION:
          return static_cast<uint32_t>(o.autoTurnOption);
        default:
          return 0;
      }
    }
    void write(uint8_t field, uint32_t value) {
      switch (static_cast<XpUiReaderParentField>(field)) {
        case XP_UI_READER_PARENT_FIELD_ORIENTATION:
          SETTINGS.orientation = static_cast<uint8_t>(value);
          return;
        case XP_UI_READER_PARENT_FIELD_SCREEN_INVERTED:
          SETTINGS.screenInverted = static_cast<uint8_t>(value);
          return;
        case XP_UI_READER_PARENT_FIELD_FRONTLIGHT_ON:
          SETTINGS.frontlightOn = static_cast<uint8_t>(value);
          return;
        case XP_UI_READER_PARENT_FIELD_TEMPORARY_COUNT:
          temporaryCount = static_cast<uint16_t>(value);
          return;
        default:
          break;
      }
      ModelLock guard(o);
      switch (static_cast<XpUiReaderParentField>(field)) {
        case XP_UI_READER_PARENT_FIELD_PENDING_TURN:
          o.pendingManualTurn = static_cast<decltype(o.pendingManualTurn)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_OVERLAY:
          o.overlay = static_cast<decltype(o.overlay)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_FOCUS:
          o.focusedTool = static_cast<decltype(o.focusedTool)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_HOLD_JUMPED:
          o.panelHoldJumped = static_cast<decltype(o.panelHoldJumped)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_CURSOR:
          o.panelCursorShown = static_cast<decltype(o.panelCursorShown)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_CACHED_SPINE:
          o.cachedSpineIndex = static_cast<decltype(o.cachedSpineIndex)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_CACHED_COUNT:
          o.cachedChapterTotalPageCount = static_cast<decltype(o.cachedChapterTotalPageCount)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_NEXT_PAGE:
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_SPINE:
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_SCREENSHOT:
          o.pendingScreenshot = static_cast<decltype(o.pendingScreenshot)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_OVERLAY_STORED:
          o.overlayPageStored = static_cast<decltype(o.overlayPageStored)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_SHOW_BOOKMARK:
          o.showBookmarkMessage = static_cast<decltype(o.showBookmarkMessage)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_BOOKMARK_TIME:
          o.bookmarkMessageTime = static_cast<decltype(o.bookmarkMessageTime)>(value);
          break;
        case XP_UI_READER_PARENT_FIELD_AUTO_OPTION:
          o.autoTurnOption = static_cast<decltype(o.autoTurnOption)>(value);
          break;
        default:
          break;
      }
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderParentCommand>(command)) {
        case XP_UI_READER_PARENT_COMMAND_LOCK: {
          assert(!lock);
          lock.emplace();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_UNLOCK: {
          lock.reset();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_RELEASE_STRIPS: {
#if defined(XP_STRIP_RENDER) && XP_STRIP_RENDER
          o.stripBuffers.releaseIdle();
#endif
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_NEW_TOOLBAR: {
          o.toolbarUi = std::make_unique<ReaderToolbarUi>(o.renderer);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_BEGIN_TOOLBAR: {
          o.toolbarUi->begin();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_DISCARD: {
          o.discardOverlayPage();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_UPDATE: {
          o.requestUpdate();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_PREPARE_POSITION: {
          position = o.chapterPosition();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_PERCENT: {
          return o.bookPercentFor(position);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_FOOTNOTES_PRESENT: {
          return !o.currentPageMetadata.footnotes().empty();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_MENU: {
          o.startActivityForResult(
              std::make_unique<EpubReaderMenuActivity>(
                  o.renderer, o.mappedInput, o.epub->getTitle(), position.displayPage(), position.totalPages, a,
                  SETTINGS.orientation, b != 0, !o.cachedBookmarks.empty(), CLIPPINGS.hasClippings()),
              [owner = &o](const ActivityResult& result) {
                owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_MENU_RESULT, 0, &result);
              });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_APPLY_ORIENTATION: {
          o.applyOrientation(static_cast<uint8_t>(a));
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_AUTO_TURN: {
          o.toggleAutoPageTurn(static_cast<uint8_t>(a));
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_OPEN_MENU: {
          o.openReaderMenu();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_CURRENT_TOC: {
          return o.currentTocIndex();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_REMEMBER_OFFSET: {
          o.rememberCurrentContentOffset();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_DROP_SECTION: {
          o.section.reset();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_CHAPTER: {
          o.startActivityForResult(
              std::make_unique<EpubReaderChapterSelectionActivity>(o.renderer, o.mappedInput, o.epub, a),
              [owner = &o](const ActivityResult& result) {
                owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_CHAPTER_RESULT, 0, &result);
              });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_CLEAR_DEFERRED: {
          o.clearDeferredReposition();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_ASSIGN_ANCHOR: {
          o.pendingAnchor = std::get<ChapterResult>(result->data).anchor;
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_TEXT: {
          o.startActivityForResult(
              std::make_unique<TextSettingsActivity>(o.renderer, o.mappedInput, &sdFontSystem.registry(),
                                                     TextSettingsActivity::Tab::Family),
              [owner = &o](const ActivityResult& result) {
                owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_TEXT_RESULT, 0, &result);
              });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_FOOTNOTES: {
          o.openFootnoteSelect(a != 0);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_PERCENT: {
          o.startActivityForResult(std::make_unique<EpubReaderPercentSelectionActivity>(o.renderer, o.mappedInput, a),
                                   [owner = &o](const ActivityResult& result) {
                                     owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_PERCENT_RESULT, 0, &result);
                                   });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_DICTIONARY: {
          o.openDictionaryWordSelect();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_PREPARE_QR: {
          assert(!qr);
          qr.emplace(o.section->getTextFromSectionFile());
          return !qr->empty();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_DROP_QR: {
          qr.reset();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_QR: {
          assert(qr);
          o.startActivityForResult(std::make_unique<QrDisplayActivity>(o.renderer, o.mappedInput, *qr),
                                   [owner = &o](const ActivityResult& result) {
                                     owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_REOPEN_RESULT, 0, &result);
                                   });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_HOME: {
          o.onGoHome();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_CLEAR_CACHE: {
          o.epub->clearCache();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SETUP_CACHE: {
          o.epub->setupCacheDir();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SAVE_PROGRESS: {
          return o.saveProgress(a, b, temporaryCount);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_LOG_SAVE_FAILED: {
          LOG_ERR("ERS", "Failed to save progress before cache clear");
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SYNC: {
          o.launchKOReaderSync();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_BOOKMARKS: {
          o.startActivityForResult(
              std::make_unique<EpubReaderBookmarksActivity>(o.renderer, o.mappedInput, o.epub, o.epub->getPath()),
              [owner = &o](const ActivityResult& result) {
                owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_PROGRESS_RESULT, 0, &result);
              });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_START_CLIP: {
          o.startClipSelection();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_OPEN_CLIPPINGS: {
          o.openClippings();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_BOOKMARK: {
          o.addBookmark();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_LOAD_BOOKMARKS: {
          o.loadCachedBookmarks();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_APPLY_PROGRESS: {
          o.applyProgressChange(std::get<ProgressChangeResult>(result->data));
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_JUMP_PERCENT: {
          o.jumpToPercent(std::get<PercentResult>(result->data).percent);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_BUILD_ITEMS: {
          EpubReaderMenuActivity::buildMenuItems(o.moreItems, a != 0, !o.cachedBookmarks.empty(),
                                                 CLIPPINGS.hasClippings());
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_ACTION_AT: {
          assert(a >= 0 && static_cast<size_t>(a) < o.moreItems.size());
          return static_cast<uint32_t>(o.moreItems[a].action);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_ERASE_ITEM: {
          assert(a >= 0 && static_cast<size_t>(a) < o.moreItems.size());
          o.moreItems.erase(o.moreItems.begin() + a);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SETTLE: {
          o.settleOverlayRefresh();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_RESTORE_PAGE: {
          o.renderer.restoreBwBuffer(false);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_ORIENTATION: {
          static constexpr StrId ids[] = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
                                          StrId::STR_LANDSCAPE_CCW};
          static_assert(std::size(ids) == CrossPointSettings::ORIENTATION_COUNT, "orientation options");
          o.overlayPopup.show(StrId::STR_ORIENTATION, ids, static_cast<int>(std::size(ids)), a,
                              [owner = &o](int index) {
                                owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_ORIENTATION_RESULT, index);
                              });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_PREPARE_LABELS: {
          assert(!labels);
          labels.emplace();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_DROP_LABELS: {
          labels.reset();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_RESERVE_LABELS: {
          assert(labels);
          labels->reserve(static_cast<size_t>(a));
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_ADD_OFF_LABEL: {
          assert(labels);
          labels->emplace_back(tr(STR_STATE_OFF));
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_ADD_RATE_LABEL: {
          assert(labels && a >= 0 && static_cast<size_t>(a) < std::size(PAGE_TURN_RATES));
          labels->push_back(std::to_string(PAGE_TURN_RATES[a]));
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SHOW_AUTO: {
          assert(labels);
          o.overlayPopup.show(StrId::STR_AUTO_TURN_PAGES_PER_MIN, *labels, a, [owner = &o](int index) {
            owner->runMenuPolicy(XP_UI_READER_PARENT_OPERATION_AUTO_RESULT, index);
          });
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_PAINT_POPUP: {
          o.paintOverlayPopup();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_TOGGLE_BLUETOOTH: {
          EpubReaderMenuActivity::toggleBluetooth();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_RENDER_OVERLAY: {
          o.renderOverlay();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_PUSH_OVERLAY: {
          o.pushOverlayRefresh();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_SAVE_SETTINGS: {
          SETTINGS.saveToFile();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_LIGHT_GET: {
          return Frontlight.isOn();
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_LIGHT_SET: {
          Frontlight.setOn(a != 0);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_DISPLAY_FAST: {
          o.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
          break;
        }
        case XP_UI_READER_PARENT_COMMAND_CLOCK: {
          return static_cast<uint32_t>(millis());
          break;
        }
      }
      return 0;
    }
  } context{*this, result, {}, {}, {}, {}};
  const XpUiReaderParentServices services{
      &context, [](void* p, uint8_t f) { return static_cast<Context*>(p)->read(f); },
      [](void* p, uint8_t f, uint32_t v) { static_cast<Context*>(p)->write(f, v); },
      [](void* p, uint8_t c, int32_t a, int32_t b) { return static_cast<Context*>(p)->command(c, a, b); }};
  xp_ui_reader_menu_parent(&services, operation, argument);
  assert(!context.lock && !context.qr && !context.labels);
}

bool EpubReaderActivity::inputOverPage() const {
  return runProjection(XP_UI_READER_PROJECTION_OPERATION_INPUT_OVER) != 0;
}
uint32_t EpubReaderActivity::runProjection(uint8_t operation, int32_t a, int32_t b, uint8_t c, void* output) const {
  struct Context {
    EpubReaderActivity& o;
    uint8_t operation;
    void* output;
    std::optional<RenderLock> lock;
    uint32_t read(uint8_t field) {
      ModelLock guard(o);
      switch (static_cast<XpUiReaderProjectionField>(field)) {
        case XP_UI_READER_PROJECTION_FIELD_HAS_BOOK:
          return static_cast<uint32_t>(!!o.epub);
        case XP_UI_READER_PROJECTION_FIELD_HAS_SECTION:
          return static_cast<uint32_t>(!!o.section);
        case XP_UI_READER_PROJECTION_FIELD_SPINE:
          return static_cast<uint32_t>(o.currentSpineIndex);
        case XP_UI_READER_PROJECTION_FIELD_NEXT_PAGE:
          return static_cast<uint32_t>(o.nextPageNumber);
        case XP_UI_READER_PROJECTION_FIELD_CACHED_COUNT:
          return static_cast<uint32_t>(o.cachedChapterTotalPageCount);
        case XP_UI_READER_PROJECTION_FIELD_PAGE:
          return static_cast<uint32_t>(o.section->currentPage);
        case XP_UI_READER_PROJECTION_FIELD_PAGE_COUNT:
          return static_cast<uint32_t>(o.section->pageCount);
        case XP_UI_READER_PROJECTION_FIELD_BUILDING:
          return static_cast<uint32_t>(o.section->isBuilding());
        case XP_UI_READER_PROJECTION_FIELD_AUTO_TURN:
          return static_cast<uint32_t>(o.automaticPageTurnActive);
        case XP_UI_READER_PROJECTION_FIELD_PENDING_SYNC_ERROR:
          return static_cast<uint32_t>(o.pendingSyncSaveError);
        case XP_UI_READER_PROJECTION_FIELD_PENDING_PAGE:
          return static_cast<uint32_t>(o.pendingPageJump.value_or(0));
        case XP_UI_READER_PROJECTION_FIELD_LAST_TURN:
          return static_cast<uint32_t>(o.lastPageTurnTime);
        case XP_UI_READER_PROJECTION_FIELD_OVERLAY_OPEN:
          return static_cast<uint32_t>(o.overlay != Overlay::None);
        case XP_UI_READER_PROJECTION_FIELD_HAS_TOUCH:
          return static_cast<uint32_t>(BoardConfig::hasTouch());
        case XP_UI_READER_PROJECTION_FIELD_LONG_PRESS_FUNCTION:
          return static_cast<uint32_t>(SETTINGS.longPressMenuFunction);
        default:
          return 0;
      }
    }
    void write(uint8_t field, uint32_t value) {
      switch (static_cast<XpUiReaderProjectionField>(field)) {
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_PAGE:
          if (operation == XP_UI_READER_PROJECTION_OPERATION_CHAPTER)
            static_cast<ChapterPosition*>(output)->pageIndex = static_cast<int32_t>(value);
          else
            static_cast<ScreenshotInfo*>(output)->currentPage = static_cast<int32_t>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_TOTAL:
          if (operation == XP_UI_READER_PROJECTION_OPERATION_CHAPTER)
            static_cast<ChapterPosition*>(output)->totalPages = static_cast<int32_t>(value);
          else
            static_cast<ScreenshotInfo*>(output)->totalPages = static_cast<int32_t>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_SPINE:
          static_cast<ScreenshotInfo*>(output)->spineIndex = static_cast<int32_t>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_PERCENT:
          static_cast<ScreenshotInfo*>(output)->progressPercent = static_cast<int32_t>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_PARAGRAPH:
          static_cast<CrossPointPosition*>(output)->paragraphIndex = static_cast<uint16_t>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_HAS_PARAGRAPH:
          static_cast<CrossPointPosition*>(output)->hasParagraphIndex = value != 0;
          return;
        case XP_UI_READER_PROJECTION_FIELD_OUTPUT_HAS_OFFSET:
          static_cast<CrossPointPosition*>(output)->hasVisibleTextOffset = value != 0;
          return;
        default:
          break;
      }
      ModelLock guard(o);
      switch (static_cast<XpUiReaderProjectionField>(field)) {
        case XP_UI_READER_PROJECTION_FIELD_SPINE:
          o.currentSpineIndex = static_cast<decltype(o.currentSpineIndex)>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_NEXT_PAGE:
          o.nextPageNumber = static_cast<decltype(o.nextPageNumber)>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_PAGE:
          o.section->currentPage = static_cast<decltype(o.section->currentPage)>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_AUTO_TURN:
          o.automaticPageTurnActive = static_cast<decltype(o.automaticPageTurnActive)>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_PENDING_SYNC_ERROR:
          o.pendingSyncSaveError = static_cast<decltype(o.pendingSyncSaveError)>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_PENDING_PAGE:
          o.pendingPageJump = static_cast<decltype(o.pendingPageJump)>(value);
          return;
        case XP_UI_READER_PROJECTION_FIELD_LAST_TURN:
          o.lastPageTurnTime = static_cast<decltype(o.lastPageTurnTime)>(value);
          return;
        default:
          return;
      }
    }
    uint32_t command(uint8_t command, int32_t a, int32_t b) {
      switch (static_cast<XpUiReaderProjectionCommand>(command)) {
        case XP_UI_READER_PROJECTION_COMMAND_BOOK_SIZE:
          return o.epub->getBookSize();
        case XP_UI_READER_PROJECTION_COMMAND_SPINE_COUNT:
          return o.epub->getSpineItemsCount();
        case XP_UI_READER_PROJECTION_COMMAND_ESTIMATE:
          return o.section->estimatedTotalPages();
        case XP_UI_READER_PROJECTION_COMMAND_TOC_CURRENT:
          return o.section->getTocIndexForPage(o.section->currentPage);
        case XP_UI_READER_PROJECTION_COMMAND_TOC_CACHED: {
          Section cached(o.epub, o.currentSpineIndex, o.renderer);
          return cached.getTocIndexForPage(o.nextPageNumber);
        }
        case XP_UI_READER_PROJECTION_COMMAND_TITLE: {
          auto& info = *static_cast<ScreenshotInfo*>(output);
          snprintf(info.title, sizeof(info.title), "%s", o.epub->getTitle().c_str());
          break;
        }
        case XP_UI_READER_PROJECTION_COMMAND_PROGRESS: {
          float fraction;
          std::memcpy(&fraction, &b, sizeof(fraction));
          const float result = o.epub->calculateProgress(a, fraction);
          uint32_t bits;
          std::memcpy(&bits, &result, sizeof(bits));
          return bits;
        }
        case XP_UI_READER_PROJECTION_COMMAND_PARAGRAPH: {
          const auto value = o.section->getParagraphIndexForPage(static_cast<uint16_t>(a));
          return value ? (0x10000u | *value) : 0;
        }
        case XP_UI_READER_PROJECTION_COMMAND_OFFSET: {
          const auto value = o.section->getVisibleTextOffsetForPage(static_cast<uint16_t>(a));
          if (value) static_cast<CrossPointPosition*>(output)->visibleTextOffset = *value;
          return value.has_value();
        }
        case XP_UI_READER_PROJECTION_COMMAND_INIT_POSITION: {
          auto& result = *static_cast<CrossPointPosition*>(output);
          result = {o.currentSpineIndex, a, b};
          result.hasResolvedSpineIndex = true;
          result.hasMappedPage = true;
          break;
        }
        case XP_UI_READER_PROJECTION_COMMAND_CREDENTIALS:
          return KOREADER_STORE.hasCredentials();
        case XP_UI_READER_PROJECTION_COMMAND_TOOLBAR_MENU:
          return o.usesToolbarMenu();
        case XP_UI_READER_PROJECTION_COMMAND_BASE_INPUT:
          return o.ReaderActivity::inputOverPage();
        case XP_UI_READER_PROJECTION_COMMAND_START_SELECTION:
          o.startClipSelection();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_OPEN_TOOLBAR:
          o.openOverlay(Overlay::Toolbar);
          break;
        case XP_UI_READER_PROJECTION_COMMAND_OPEN_MENU:
          o.openReaderMenu();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_SAVE_ERROR_POPUP:
          GUI.drawPopup(o.renderer, tr(STR_SAVE_PROGRESS_FAILED));
          break;
        case XP_UI_READER_PROJECTION_COMMAND_LOCK:
          lock.emplace();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_UNLOCK:
          lock.reset();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_CLEAR_DEFERRED:
          o.clearDeferredReposition();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_ACKNOWLEDGE:
          bleturner::acknowledgeLinkNote();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_DROP_SECTION:
          o.section.reset();
          break;
        case XP_UI_READER_PROJECTION_COMMAND_CLOCK:
          return millis();
        default:
          break;
      }
      return 0;
    }
  } context{const_cast<EpubReaderActivity&>(*this), operation, output, {}};
  const XpUiReaderProjectionServices services{
      &context, [](void* c, uint8_t f) -> uint32_t { return static_cast<Context*>(c)->read(f); },
      [](void* c, uint8_t f, uint32_t v) { static_cast<Context*>(c)->write(f, v); },
      [](void* c, uint8_t op, int32_t a, int32_t b) -> uint32_t {
        return static_cast<Context*>(c)->command(op, a, b);
      }};
  const uint32_t result = xp_ui_reader_projection(&services, operation, a, b, c);
  assert(!context.lock);
  return result;
}
