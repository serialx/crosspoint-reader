#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "ProgressFile.h"
#include "ReaderActivity.h"
#include "ReaderUtils.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
bool drawPagePixels(GfxRenderer& renderer, const uint8_t* bytes, uint32_t length, uint16_t width, uint16_t height,
                    uint8_t depth, uint8_t pass) {
  const XpUiXtcDiagnostics diagnostics{&renderer, [](void* p, int32_t x, int32_t y, uint8_t black) {
                                         static_cast<GfxRenderer*>(p)->drawPixel(x, y, black);
                                       }};
  struct Context {
    const uint8_t* bytes;
    uint32_t length;
    uint16_t width, height;
    uint8_t depth, pass;
    const XpUiXtcDiagnostics* diagnostics;
    bool painted = false;
  } context{bytes, length, width, height, depth, pass, &diagnostics};
  XpUiBootLayout layout{};
  layout.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                   static_cast<int16_t>(renderer.getScreenHeight())};
  layout.view = XP_UI_BOOT_XTC_PAGE;
  XpUiCommand command{};
  const XpUiBuffer buffer{&command, nullptr, 1, 0};
  XpUiScene scene{};
  const XpUiBootMetrics metrics{nullptr, [](void*, uint32_t, int32_t, uint8_t) -> int32_t { return 0; }};
  const XpUiPaint paint{&context, nullptr, nullptr,
                        [](void*, uint32_t h, uint32_t g) -> uint8_t { return h == XP_UI_BOOT_XTC_PIXELS && g == 1; },
                        [](void* p, const XpGlyphTarget* target, const XpUiCommand*, XpUiRect) {
                          auto& c = *static_cast<Context*>(p);
                          c.painted = xp_ui_xtc_pixels(target, c.bytes, c.length, c.width, c.height, c.depth, c.pass,
                                                       c.diagnostics);
                        }};
  return xp_ui_boot_view(&layout, &metrics, &buffer, &scene) && renderer.drawUiScene(scene, &paint) && context.painted;
}
void drawPageError(GfxRenderer& renderer, const char* text) {
  struct Context {
    GfxRenderer& renderer;
    const char* text;
  } context{renderer, text};
  XpUiBootLayout layout{};
  layout.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                   static_cast<int16_t>(renderer.getScreenHeight())};
  layout.error_font = UI_12_FONT_ID;
  layout.line_height = renderer.getLineHeight(UI_12_FONT_ID);
  layout.view = XP_UI_BOOT_PAGE_ERROR;
  layout.refresh = XP_UI_FAST;
  XpUiCommand commands[2]{};
  const XpUiBuffer buffer{commands, nullptr, 2, 0};
  XpUiScene scene{};
  const XpUiBootMetrics metrics{&context, [](void* p, uint32_t handle, int32_t font, uint8_t style) -> int32_t {
                                  const auto& c = *static_cast<Context*>(p);
                                  return handle == XP_UI_BOOT_ERROR
                                             ? c.renderer.getTextWidth(font, c.text,
                                                                       static_cast<EpdFontFamily::Style>(style))
                                             : -1;
                                }};
  const XpUiPaint paint{&context, nullptr, nullptr,
                        [](void*, uint32_t h, uint32_t g) -> uint8_t { return h == XP_UI_BOOT_ERROR && g == 1; },
                        [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
                          auto& c = *static_cast<Context*>(p);
                          c.renderer.drawUiText(*command, clip, c.text);
                        }};
  if (!xp_ui_boot_view(&layout, &metrics, &buffer, &scene) || !renderer.drawUiScene(scene, &paint)) {
    LOG_ERR("RUI", "Cannot build XTC error scene");
    return;
  }
  renderer.displayBuffer();
}
}  // namespace

bool XtcReaderActivity::loadBook() {
  auto loadedXtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
  if (!loadedXtc) {
    LOG_ERR("XTR", "Failed to allocate XTC object");
    return false;
  }
  if (!loadedXtc->load()) {
    LOG_ERR("XTR", "Failed to load XTC");
    return false;
  }
  xtc = std::move(loadedXtc);
  xtc->setupCacheDir();
  loadProgress();
  return true;
}

void XtcReaderActivity::openChapterSelection() {
  if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
    startActivityForResult(std::make_unique<XtcReaderChapterSelectionActivity>(renderer, mappedInput, xtc, state.page),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               state.page = std::get<PageResult>(result.data).page;
                               requestUpdate();
                             }
                           });
  }
}

bool XtcReaderActivity::handleFormatInput() {
  if (!xtc) {
    return false;
  }

  // Enter chapter selection activity on Confirm release or touch menu gesture
  const bool touchMenu = ReaderUtils::isTouchMenuGesture(renderer, mappedInput);
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || touchMenu) {
    if (touchMenu && xtc->hasChapters() && !xtc->getChapters().empty()) haptic_feedback::touchAction();
    openChapterSelection();
    return true;
  }
  return false;
}

void XtcReaderActivity::applyInitialOrientation() { renderer.setOrientation(GfxRenderer::Orientation::Portrait); }

void XtcReaderActivity::renderBook() {
  if (!xtc) {
    return;
  }

  renderPage();
  saveProgress();
}

XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo() const {
  const auto sb = SETTINGS.statusBarSpec();
  const int bookPageCount = static_cast<int>(xtc->getPageCount());
  const int bookPage = static_cast<int>(state.page) + 1;
  std::string title = sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::BOOK_TITLE ? xtc->getTitle() : "";

  if (!xtc->hasChapters()) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  const auto& chapters = xtc->getChapters();
  const auto chapterIt = std::find_if(chapters.begin(), chapters.end(), [this](const xtc::ChapterInfo& chapter) {
    return state.page >= chapter.startPage && state.page <= chapter.endPage;
  });

  if (chapterIt == chapters.end() || chapterIt->endPage < chapterIt->startPage) {
    return StatusBarInfo{bookPage, bookPageCount, std::move(title)};
  }

  if (sb.titleMode == CrossPointSettings::STATUS_BAR_TITLE::CHAPTER_TITLE) {
    title = chapterIt->name.empty() ? tr(STR_UNNAMED) : chapterIt->name;
  }

  return StatusBarInfo{static_cast<int>(state.page - chapterIt->startPage) + 1,
                       static_cast<int>(chapterIt->endPage - chapterIt->startPage) + 1, std::move(title)};
}

void XtcReaderActivity::renderStatusBarOverlay(GfxRenderer& renderer, const StatusBarOverlayPosition position) const {
  const auto sb = SETTINGS.statusBarSpec();
  const int statusBarHeight = UITheme::getInstance().getStatusBarHeight();
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const auto overlay = xp_ui_xtc_overlay(renderer.getScreenWidth(), renderer.getScreenHeight(), top, bottom,
                                         statusBarHeight, sb.xtcMode, position == StatusBarOverlayPosition::Top);
  if (!overlay.draw) return;
  if (overlay.clear.h > 0) renderer.fillRect(overlay.clear.x, overlay.clear.y, overlay.clear.w, overlay.clear.h, false);

  const int pageCount = static_cast<int>(xtc->getPageCount());
  const int displayPage = static_cast<int>(state.page) + 1;
  const float progress = pageCount > 0 ? (static_cast<float>(displayPage) * 100.0f) / pageCount : 0.0f;
  const auto pageInfo = getStatusBarInfo();
  GUI.drawStatusBar(renderer, progress, pageInfo.currentPage, pageInfo.pageCount, pageInfo.title,
                    overlay.padding_bottom);
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = static_cast<size_t>(pageWidth) * ((static_cast<size_t>(pageHeight) + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    LOG_ERR("XTR", "Failed to allocate page buffer (%lu bytes)", pageBufferSize);
    drawPageError(renderer, tr(STR_MEMORY_ERROR));
    return;
  }

  size_t bytesRead = xtc->loadPage(state.page, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    LOG_ERR("XTR", "Failed to load page %lu: bufferSize=%lu bitDepth=%u error=%s", state.page, pageBufferSize, bitDepth,
            xtc::errorToString(xtc->getLastError()));
    free(pageBuffer);
    drawPageError(renderer, tr(STR_PAGE_LOAD_ERROR));
    return;
  }

  renderer.clearScreen();

  const auto drawPage = [&](const uint8_t pass) {
    if (drawPagePixels(renderer, pageBuffer, pageBufferSize, pageWidth, pageHeight, bitDepth, pass)) return true;
    LOG_ERR("XTR", "Invalid XTC page paint target");
    return false;
  };
  if (bitDepth == 2) {
    if (!drawPage(0)) {
      free(pageBuffer);
      return;
    }

    if (pagesUntilFullRefresh <= 1) {
      // Periodic ghost cleanup: scrub via the normal path, then run the
      // settle flavor of the grayscale base pass (DTM planes are equal after
      // the display sync, so only the gentle reinforcement cells fire).
      // Combined-base panels (Paper Mono) instead defer the base so the gray
      // planes below join it in one waveform.
      if (renderer.grayscaleCapabilities().base == HalDisplay::GrayscaleBase::Combined) {
        renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        renderer.preconditionGrayscale();
      }
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
      pagesUntilFullRefresh--;
    }

    renderer.clearScreen(0x00);
    if (!drawPage(1)) {
      free(pageBuffer);
      return;
    }
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    if (!drawPage(2)) {
      free(pageBuffer);
      return;
    }
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();

    renderer.clearScreen();
    if (!drawPage(0)) {
      free(pageBuffer);
      return;
    }

    renderer.cleanupGrayscaleWithFrameBuffer();

    free(pageBuffer);

    LOG_DBG("XTR", "Rendered page %lu/%lu (2-bit grayscale)", state.page + 1, xtc->getPageCount());
    markPageRendered();
    return;
  } else {
    if (!drawPage(0)) {
      free(pageBuffer);
      return;
    }
  }

  free(pageBuffer);

  if (SETTINGS.statusBarSpec().xtcMode == CrossPointSettings::XTC_STATUS_BAR_MODE::XTC_STATUS_BAR_TOP) {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Top);
  } else {
    renderStatusBarOverlay(renderer, StatusBarOverlayPosition::Bottom);
  }

  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  LOG_DBG("XTR", "Rendered page %lu/%lu (%u-bit)", state.page + 1, xtc->getPageCount(), bitDepth);
  markPageRendered();
}

bool XtcReaderActivity::pageTurn(bool isForward) {
  return xtc && xp_ui_xtc_turn(&state, xtc->getPageCount(), isForward ? 1 : -1, false);
}

bool XtcReaderActivity::skipPages(int amount) {
  return xtc && xp_ui_xtc_turn(&state, xtc->getPageCount(), amount, true);
}

bool XtcReaderActivity::isAtEndOfBook() const { return xtc && state.page >= xtc->getPageCount(); }

void XtcReaderActivity::onReturnFromEndOfBook() { xp_ui_xtc_restore(&state, xtc ? xtc->getPageCount() : 0, true); }

void XtcReaderActivity::saveProgress() const {
  if (!xtc) return;
  uint8_t data[4];
  data[0] = state.page & 0xFF;
  data[1] = (state.page >> 8) & 0xFF;
  data[2] = (state.page >> 16) & 0xFF;
  data[3] = (state.page >> 24) & 0xFF;
  if (!ProgressFile::writeAtomic(xtc->getCachePath(), data, sizeof(data))) {
    LOG_ERR("XTC", "Failed to save progress: page %lu", state.page);
  }
}

void XtcReaderActivity::loadProgress() {
  if (!xtc) return;
  HalFile f;
  if (Storage.openFileForRead("XTC", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      state.page = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      xp_ui_xtc_restore(&state, xtc->getPageCount(), false);
      LOG_DBG("XTC", "Loaded progress: page %lu/%lu", state.page + 1, xtc->getPageCount());
    }
  }
}

ScreenshotInfo XtcReaderActivity::getScreenshotInfo() const {
  ScreenshotInfo info;
  info.readerType = ScreenshotInfo::ReaderType::Xtc;
  if (xtc) {
    const std::string t = xtc->getTitle();
    snprintf(info.title, sizeof(info.title), "%s", t.c_str());
    const uint32_t pageCount = xtc->getPageCount();
    info.totalPages = pageCount;
    uint32_t clampedPage = (pageCount > 0 && state.page >= pageCount) ? pageCount - 1 : state.page;
    info.progressPercent = pageCount > 0 ? xtc->calculateProgress(clampedPage) : 0;
    info.currentPage = static_cast<int>(clampedPage) + 1;
  } else {
    info.currentPage = state.page + 1;
  }
  return info;
}
