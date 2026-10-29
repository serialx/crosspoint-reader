#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Memory.h>
#include <Utf8.h>
#include <Xtc.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HapticFeedback.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/cover.h"
#include "components/icons/folder.h"
#include "components/icons/library.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "fontIds.h"

namespace {
XpUiHomeWidths coverWidths{};
constexpr uint32_t WRAPPED_TITLE = 0x10000;
constexpr uint32_t CLIPPED_AUTHOR = 0x11000;
constexpr uint32_t CLIPPED_MENU = 0x12000;
XpUiRect uiRect(Rect r) {
  return {static_cast<int16_t>(r.x), static_cast<int16_t>(r.y), static_cast<int16_t>(r.width),
          static_cast<int16_t>(r.height)};
}
}  // namespace

XpUiInterest HomeActivity::rustInterest(uint32_t now) const { return xp_ui_home_interest(&state, now); }

XpUiEffect HomeActivity::updateRust(const XpUiInput& input) {
  const XpUiHomeTouch empty{};
  const auto update = coverGridUi ? xp_ui_home_grid_update(&state, &input, publication.ready ? &publishedGrid : nullptr)
                                  : xp_ui_home_update(&state, &input, publication.ready ? &publishedTouch : &empty);
  touchActivated = update.haptic;
  return update.effect;
}

void HomeActivity::applyRustEffect(const XpUiEffect& effect) {
  bool touch;
  {
    ModelLock lock(*this);
    touch = touchActivated;
    touchActivated = false;
  }
  if (touch) haptic_feedback::touchAction(false);
  if (effect.kind != XP_UI_ACTIVATE) return;
  if (effect.value >= 0 && static_cast<size_t>(effect.value) < recentBooks.size()) {
    onRecentBookSelected(recentBooks[effect.value].path);
    return;
  }
  switch (effect.value) {
    case XP_UI_HOME_FILES:
      onFileBrowserOpen();
      break;
    case XP_UI_HOME_LIBRARY:
      onLibraryOpen();
      break;
    case XP_UI_HOME_OPDS:
      onOpdsBrowserOpen();
      break;
    case XP_UI_HOME_PLUGINS:
      onPluginsOpen();
      break;
    case XP_UI_HOME_TRANSFER:
      onFileTransferOpen();
      break;
    case XP_UI_HOME_SETTINGS:
      onSettingsOpen();
      break;
  }
}

void HomeActivity::sampleLayout() {
  const auto& m = UITheme::getInstance().getMetrics();
  frame.layout = {};
  auto& l = frame.layout;
  l.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())};
  l.content = uiRect(UITheme::getContentArea(renderer));
  l.safe = uiRect(UITheme::getInstance().getScreenSafeArea(renderer, true));
  l.title_font = UI_12_FONT_ID;
  l.small_font = UI_10_FONT_ID;
  l.tiny_font = SMALL_FONT_ID;
  l.top_padding = m.topPadding;
  l.header_height = m.headerHeight;
  l.home_top = m.homeTopPadding;
  l.tile_height = m.homeCoverTileHeight;
  l.cover_height = m.homeCoverHeight;
  l.side_padding = m.contentSidePadding;
  l.spacing = m.verticalSpacing;
  l.menu_top = m.homeMenuTopOffset;
  l.menu_height = m.menuRowHeight;
  l.menu_gap = m.menuSpacing;
  l.hints_height = m.buttonHintsHeight;
  l.scroll_width = m.scrollBarWidth;
  l.scroll_offset = m.scrollBarRightOffset;
  l.theme = GUI.homeThemeKind();
  l.continue_in_menu = m.homeContinueReadingInMenu;
  l.columns = m.homeRecentBooksCount;
}

void HomeActivity::prepareCovers() {
  auto& p = frame.covers;
  p = {};
  p.rendered = coverRendered;
  frame.restoreRect = {static_cast<int16_t>(coverRectX), static_cast<int16_t>(coverRectY),
                       static_cast<int16_t>(coverRectW), static_cast<int16_t>(coverRectH)};
  const size_t restoreBytes = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  p.restored = coverRendered && coverBufferStored && coverBuffer && renderer.hasFrameBuffer() && restoreBytes > 0 &&
               coverBufferSize >= restoreBytes;
  const auto& l = frame.layout;
  coverRectX = l.content.x;
  coverRectY = l.home_top;
  coverRectW = l.content.w;
  coverRectH = l.tile_height;
  const int thumbHeight = xp_ui_home_thumb_height(&l);
  if (!coverRendered || l.theme == 0) {
    for (size_t i = 0; i < std::min<size_t>(recentBooks.size(), 3); ++i) {
      if (recentBooks[i].coverBmpPath.empty()) continue;
      auto& cover = p.covers[i];
      cover.status = 1;
      HalFile file;
      const auto path = UITheme::getCoverThumbPath(recentBooks[i].coverBmpPath, thumbHeight);
      if (Storage.openFileForRead("HOME", path, file)) {
        Bitmap bitmap(file);
        cover.status = bitmap.parseHeaders() == BmpReaderError::Ok ? 2 : 0;
        cover.width = bitmap.getWidth();
        cover.height = bitmap.getHeight();
      }
    }
  }
  xp_ui_home_prepare_widths(&coverWidths, &frame.snapshot, &l, &p);
  if (!coverRendered && !recentBooks.empty() && (l.theme != 0 || p.covers[0].status == 2))
    p.can_snapshot = allocateCoverBuffer();
}

void HomeActivity::prepareTitle(uint8_t book) {
  if (frame.titleBook == book) return;
  const auto& wrap = frame.titleWraps[book];
  // Only one book's wrapped lines are needed by a synchronous text callback.
  std::vector<std::string>().swap(frame.titleLines);
  frame.titleLines = renderer.wrappedText(wrap.font, recentBooks[book].title.c_str(), wrap.width, wrap.count,
                                          static_cast<EpdFontFamily::Style>(wrap.style));
  frame.titleBook = book;
}

const char* HomeActivity::text(uint32_t handle) {
  if (handle >= XP_UI_HOME_TITLE && handle < XP_UI_HOME_TITLE + 3) {
    const size_t i = handle - XP_UI_HOME_TITLE;
    return i < recentBooks.size() ? recentBooks[i].title.c_str() : nullptr;
  }
  if (handle >= XP_UI_HOME_AUTHOR && handle < XP_UI_HOME_AUTHOR + 3) {
    const size_t i = handle - XP_UI_HOME_AUTHOR;
    return i < recentBooks.size() ? recentBooks[i].author.c_str() : nullptr;
  }
  if (handle >= WRAPPED_TITLE && handle < WRAPPED_TITLE + 12) {
    const auto book = static_cast<uint8_t>((handle - WRAPPED_TITLE) / 4);
    if (book >= recentBooks.size()) return nullptr;
    prepareTitle(book);
    const size_t i = (handle - WRAPPED_TITLE) % 4;
    return i < frame.titleLines.size() ? frame.titleLines[i].c_str() : nullptr;
  }
  if (handle == CLIPPED_AUTHOR) return frame.authorLine.c_str();
  if (handle >= CLIPPED_MENU && handle < CLIPPED_MENU + frame.menuCount)
    return frame.menuLines[handle - CLIPPED_MENU].c_str();
  if (handle >= XP_UI_HOME_MENU && handle < XP_UI_HOME_MENU + frame.menuCount) {
    switch (frame.menuKeys[handle - XP_UI_HOME_MENU]) {
      case 0:
        return tr(STR_CONTINUE_READING);
      case 1:
        return tr(STR_BROWSE_FILES);
      case 2:
        return tr(STR_LIBRARY);
      case 3:
        return tr(STR_PLUGINS);
      case 4:
        return tr(STR_OPDS_BROWSER);
      case 5:
        return tr(STR_FILE_TRANSFER);
      case 6:
        return tr(STR_SETTINGS_TITLE);
    }
  }
  switch (handle) {
    case XP_UI_HOME_NO_OPEN_BOOK:
      return tr(STR_NO_OPEN_BOOK);
    case XP_UI_HOME_START_READING:
      return tr(STR_START_READING);
    case XP_UI_HOME_CONTINUE:
      return tr(STR_CONTINUE_READING);
    default:
      return nullptr;
  }
}

XpUiHomeTexts HomeActivity::textServices() {
  return {this,
          [](void* p, uint32_t h, int32_t f, uint8_t b) -> int32_t {
            auto& self = *static_cast<HomeActivity*>(p);
            const char* s = self.text(h);
            return s ? self.renderer.getTextWidth(f, s, static_cast<EpdFontFamily::Style>(b)) : -1;
          },
          [](void* p, int32_t f) -> int32_t { return static_cast<HomeActivity*>(p)->renderer.getLineHeight(f); },
          [](void* p, uint32_t h) -> uint8_t {
            const char* s = static_cast<HomeActivity*>(p)->text(h);
            return !s || !*s;
          },
          [](void* p, uint32_t h, int32_t f, int16_t w, uint8_t n, uint8_t b) -> XpUiHomeLines {
            auto& self = *static_cast<HomeActivity*>(p);
            if (h < XP_UI_HOME_TITLE || h >= XP_UI_HOME_TITLE + 3) return {};
            const char* s = self.text(h);
            if (!s) return {};
            const auto book = static_cast<uint8_t>(h - XP_UI_HOME_TITLE);
            self.frame.titleWraps[book] = {f, w, n, b};
            self.frame.titleBook = 255;
            self.prepareTitle(book);
            const auto& lines = self.frame.titleLines;
            self.frame.titleLineCounts[book] = lines.size();
            int width = 0;
            for (const auto& line : lines)
              width =
                  std::max(width, self.renderer.getTextWidth(f, line.c_str(), static_cast<EpdFontFamily::Style>(b)));
            return {WRAPPED_TITLE + 4 * (h - XP_UI_HOME_TITLE), static_cast<uint8_t>(lines.size()), 0,
                    static_cast<int16_t>(width)};
          },
          [](void* p, uint32_t h, int32_t f, int16_t w, uint8_t b) -> uint32_t {
            auto& self = *static_cast<HomeActivity*>(p);
            const char* s = self.text(h);
            if (!s) return 0;
            if (h == XP_UI_HOME_AUTHOR) {
              self.frame.authorLine = self.renderer.truncatedText(f, s, w, static_cast<EpdFontFamily::Style>(b));
              return CLIPPED_AUTHOR;
            }
            if (h >= XP_UI_HOME_MENU && h < XP_UI_HOME_MENU + self.frame.menuCount) {
              self.frame.menuLines[h - XP_UI_HOME_MENU] =
                  self.renderer.truncatedText(f, s, w, static_cast<EpdFontFamily::Style>(b));
              return CLIPPED_MENU + h - XP_UI_HOME_MENU;
            }
            return 0;
          }};
}

bool HomeActivity::buildRustScene(XpUiScene& scene) {
  {
    ModelLock lock(*this);
    frame.snapshot = state;
  }
  if (coverGridUi) {
    const auto layout = coverGridUi->layout();
    const auto texts = coverGridUi->textServices();
    if (!frame.commands.ready() && !frame.commands.reserve(24)) return false;
    for (;;) {
      const auto buffer = frame.commands.buffer();
      const auto status =
          xp_ui_home_grid_view(&frame.snapshot, &layout, &texts, &buffer, cleanInitialRefresh, &frame.grid, &scene);
      if (status == XP_UI_HOME_VIEW_OK) return true;
      if (status != XP_UI_HOME_VIEW_CAPACITY || buffer.command_capacity >= 128 ||
          !frame.commands.reserve(std::min<uint16_t>(128, buffer.command_capacity + 4)))
        return false;
    }
  }
  sampleLayout();
  prepareCovers();
  frame.menuCount = xp_ui_home_menu_keys(&frame.snapshot, frame.layout.continue_in_menu, frame.menuKeys.data(),
                                         frame.menuKeys.size());
  const auto texts = textServices();
  if (!frame.commands.ready() && !frame.commands.reserve(24)) return false;
  for (;;) {
    const auto buffer = frame.commands.buffer();
    const auto status = xp_ui_home_standard_view(&frame.snapshot, &frame.layout, &frame.covers, &texts, &buffer,
                                                 cleanInitialRefresh, &frame.touch, &scene);
    if (status == XP_UI_HOME_VIEW_OK) return true;
    if (status != XP_UI_HOME_VIEW_CAPACITY || buffer.command_capacity >= 128 ||
        !frame.commands.reserve(std::min<uint16_t>(128, buffer.command_capacity + 4)))
      return false;
  }
}

void HomeActivity::rustPublished() {
  publishedTouch = frame.touch;
  publishedGrid = frame.grid;
}

XpUiPaint HomeActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t h, uint32_t generation) -> uint8_t {
            auto& self = *static_cast<HomeActivity*>(p);
            if (h >= WRAPPED_TITLE && h < WRAPPED_TITLE + 12) {
              const auto book = (h - WRAPPED_TITLE) / 4;
              return generation == 1 && book < self.recentBooks.size() &&
                     (h - WRAPPED_TITLE) % 4 < self.frame.titleLineCounts[book];
            }
            return generation == 1 &&
                   ((self.coverGridUi &&
                     (self.coverGridUi->text(h) ||
                      (h >= XP_UI_HOME_GRID_COVER && h < XP_UI_HOME_GRID_COVER + self.recentBooks.size()) ||
                      (h >= XP_UI_HOME_GRID_ICON && h < XP_UI_HOME_GRID_ICON + 6))) ||
                    self.text(h) || (h >= XP_UI_HOME_HEADER && h <= XP_UI_HOME_COVER_ICON) ||
                    (h >= XP_UI_HOME_COVER && h < XP_UI_HOME_COVER + self.recentBooks.size()) ||
                    (h >= XP_UI_HOME_MENU_ICON && h < XP_UI_HOME_MENU_ICON + self.frame.menuCount));
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<HomeActivity*>(p);
            const auto& r = command->rect;
            const Rect rect{r.x, r.y, r.w, r.h};
            const uint32_t h = command->resource;
            if (self.coverGridUi) {
              if (const char* s = self.coverGridUi->text(h)) {
                self.renderer.drawUiText(*command, clip, s);
                return;
              }
              if ((h >= XP_UI_HOME_GRID_COVER && h < XP_UI_HOME_GRID_COVER + self.recentBooks.size()) ||
                  (h >= XP_UI_HOME_GRID_ICON && h < XP_UI_HOME_GRID_ICON + 6)) {
                self.coverGridUi->paint(h, r);
                return;
              }
            }
            if (const char* s = self.text(h)) {
              self.renderer.drawUiText(*command, clip, s);
              return;
            }
            if (h >= XP_UI_HOME_COVER && h < XP_UI_HOME_COVER + self.recentBooks.size()) {
              HalFile file;
              const auto path = UITheme::getCoverThumbPath(self.recentBooks[h - XP_UI_HOME_COVER].coverBmpPath,
                                                           xp_ui_home_thumb_height(&self.frame.layout));
              if (Storage.openFileForRead("HOME", path, file)) {
                Bitmap bitmap(file);
                if (bitmap.parseHeaders() == BmpReaderError::Ok) {
                  if (self.frame.layout.theme == 0) LOG_DBG("THEME", "Rendering bmp");
                  GUI.drawCoverThumbFill(self.renderer, bitmap, rect);
                  if (self.frame.layout.theme == 0 && self.frame.snapshot.selected == 0)
                    LOG_DBG("THEME", "Drawing selection");
                }
              }
              return;
            }
            if (h >= XP_UI_HOME_MENU_ICON && h < XP_UI_HOME_MENU_ICON + self.frame.menuCount) {
              static constexpr const uint8_t* icons[] = {BookIcon,   FolderIcon,   LibraryIcon,  BlocksIcon,
                                                         BlocksIcon, TransferIcon, Settings2Icon};
              self.renderer.drawIcon(icons[self.frame.menuKeys[h - XP_UI_HOME_MENU_ICON]], r.x, r.y, r.w);
              return;
            }
            switch (h) {
              case XP_UI_HOME_HEADER:
                GUI.drawHeader(self.renderer, rect,
                               !self.coverGridUi && self.frame.layout.continue_in_menu && !self.recentBooks.empty()
                                   ? self.recentBooks[0].title.c_str()
                                   : nullptr,
                               nullptr, false);
                break;
              case XP_UI_HOME_FOOTER: {
                const auto labels =
                    self.mappedInput.mapLabels(self.frame.snapshot.continuing ? tr(STR_RESUME) : "", tr(STR_SELECT),
                                               self.coverGridUi ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                                               self.coverGridUi ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
                GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
                break;
              }
              case XP_UI_HOME_RESTORE: {
                const auto& old = self.frame.restoreRect;
                self.renderer.copyBufferToRegion(old.x, old.y, old.w, old.h, self.coverBuffer, self.coverBufferSize);
                break;
              }
              case XP_UI_HOME_SNAPSHOT:
                self.coverBufferStored = self.storeCoverBuffer();
                self.coverRendered = self.coverBufferStored;
                break;
              case XP_UI_HOME_MASK_CORNERS:
                self.renderer.maskRoundedRectOutsideCorners(r.x, r.y, r.w, r.h, 18, Color::LightGray);
                break;
              case XP_UI_HOME_RIBBON: {
                const int xs[] = {r.x, r.x + r.w, r.x + r.w, r.x + r.w / 2, r.x};
                const int ys[] = {r.y, r.y, r.y + r.h, r.y + r.h - r.h / 3, r.y + r.h};
                self.renderer.fillPolygon(xs, ys, 5, command->paint != 1);
                break;
              }
              case XP_UI_HOME_PLACEHOLDER:
                GUI.drawCoverPlaceholder(self.renderer, rect);
                break;
              case XP_UI_HOME_COVER_ICON:
                self.renderer.drawIcon(CoverIcon, r.x, r.y, r.w);
                break;
            }
          }};
}

void HomeActivity::render(RenderLock&&) {
  XpUiScene scene{};
  if (!buildRustScene(scene)) {
    rejectRustScene();
    LOG_ERR("RUI", "Cannot build scene in Home");
    return;
  }
  const bool thumbChanged = coverGridUi && coverGridUi->prepare(frame.grid);
  if (!paintRustScene(scene)) return;
  if (thumbChanged) coverGridUi->refreshCoverPaths();
  XpUiEffect after{};
  {
    ModelLock lock(*this);
    after = xp_ui_home_painted(&state, thumbChanged);
  }
  if (after.invalidate) requestUpdate();
  if (after.kind == 4) {
    loadRecentCovers(coverGridUi ? CoverGridHomeUi::THUMB_HEIGHT : xp_ui_home_thumb_height(&frame.layout));
    if (coverGridUi) {
      coverGridUi->refreshCoverPaths();
      requestUpdate();
    }
  }
}

void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(coverGridUi ? maxBooks : std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (RecentBooksStore::isMissing(book)) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::fillCoverGridFromLibrary() {
  if (recentBooks.size() >= CoverGridHomeUi::MAX_BOOKS) return;
  // Keep the index and record together off the task stack; reuse for every row.
  struct LibraryReader {
    library::LibraryIndexFile index;
    library::ClixRecord record;
  };
  auto reader = makeUniqueNoThrow<LibraryReader>();
  if (!reader) {
    LOG_ERR("HOME", "OOM: library index");
    return;
  }
  auto& index = reader->index;
  auto& record = reader->record;
  if (!index.open(library::libraryIndexPath())) {
    index.close();
    GUI.drawPopup(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    if (!library::buildLibraryIndex("/", stats, SETTINGS.libraryUseMetadata != 0) ||
        !index.open(library::libraryIndexPath())) {
      LOG_ERR("HOME", "Cannot populate cover grid from library");
      return;
    }
  }
  for (uint16_t row = 0; row < index.bookCount() && recentBooks.size() < CoverGridHomeUi::MAX_BOOKS; ++row) {
    RecentBook book;
    if (!index.readRecord(index.ordinalForRow(library::SortOrder::RecentDesc, row), record) ||
        !index.readPath(record, book.path))
      continue;
    if (std::any_of(recentBooks.begin(), recentBooks.end(),
                    [&](const RecentBook& existing) { return existing.path == book.path; }) ||
        RecentBooksStore::isMissing(book))
      continue;
    if (!index.readTitle(record, book.title) && !index.readName(record, book.title)) continue;
    index.readAuthor(record, book.author);
    if (index.ioFailed()) break;
    recentBooks.push_back(std::move(book));
  }
}

void HomeActivity::resolveGridCoverPaths() {
  for (auto& book : recentBooks) {
    if (!book.coverBmpPath.empty()) continue;
    // Constructors only derive cache paths; no metadata parsing or image generation.
    // Keep these large objects off the task stack and release each before the next book.
    if (FsHelpers::hasReflowableBookExtension(book.path)) {
      auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
      if (!epub) {
        LOG_ERR("HOME", "OOM: EPUB thumbnail path");
        continue;
      }
      book.coverBmpPath = epub->getThumbBmpPath();
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
      if (!xtc) {
        LOG_ERR("HOME", "OOM: XTC thumbnail path");
        continue;
      }
      book.coverBmpPath = xtc->getThumbBmpPath();
    }
  }
}

void HomeActivity::loadGridCover(RecentBook& book, int height, bool& showingLoading, Rect& popupRect) {
  if (!book.coverBmpPath.empty() && Storage.exists(UITheme::getCoverThumbPath(book.coverBmpPath, height).c_str()))
    return;
  // Only one parser lives at a time; EPUB/XTC objects exceed the stack budget.
  if (FsHelpers::hasReflowableBookExtension(book.path)) {
    auto epub = makeUniqueNoThrow<Epub>(book.path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: cover EPUB");
      return;
    }
    book.coverBmpPath = epub->getThumbBmpPath();
    if (Storage.exists(epub->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (epub->generateThumbBmpFromSource(height)) {
      return;
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(book.path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: cover XTC");
      return;
    }
    book.coverBmpPath = xtc->getThumbBmpPath();
    if (Storage.exists(xtc->getThumbBmpPath(height).c_str())) return;
    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
      GUI.fillPopupProgress(renderer, popupRect, 0);
    }
    if (xtc->load() && xtc->generateThumbBmp(height)) {
      return;
    }
  }
  book.coverBmpPath.clear();
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  bool showingLoading = false;
  Rect popupRect;

  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // The cover grid shares one slot size; generating at any other height
    // would rescale the dithered thumb at draw time and alias badly.
    const int thumbHeight = coverGridUi ? coverGridUi->thumbHeightFor() : coverHeight;
    if (coverGridUi) {
      loadGridCover(book, thumbHeight, showingLoading, popupRect);
      ++progress;
      if (showingLoading) GUI.fillPopupProgress(renderer, popupRect, progress * 100 / recentBooks.size());
      continue;
    }
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, thumbHeight);
      if (!Storage.exists(coverPath.c_str())) {
        // If epub/txt/md, try to load the metadata for title/author and cover
        if (FsHelpers::hasReflowableBookExtension(book.path)) {
          Epub epub(book.path, "/.crosspoint");
          // Skip loading css since we only need metadata here
          epub.load(false, true);

          // Try to generate thumbnail image for Continue Reading card
          if (!showingLoading) {
            showingLoading = true;
            popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
          }
          GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
          bool success = epub.generateThumbBmp(thumbHeight);
          if (!success) {
            RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
            book.coverBmpPath = "";
          }
          coverRendered = false;
          requestUpdate();
        } else if (FsHelpers::hasXtcExtension(book.path)) {
          // Handle XTC file
          Xtc xtc(book.path, "/.crosspoint");
          if (xtc.load()) {
            // Try to generate thumbnail image for Continue Reading card
            if (!showingLoading) {
              showingLoading = true;
              popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
            }
            GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
            bool success = xtc.generateThumbBmp(thumbHeight);
            if (!success) {
              RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
              book.coverBmpPath = "";
            }
            coverRendered = false;
            requestUpdate();
          }
        }
      }
    }
    progress++;
  }

  {
    ModelLock lock(*this);
    xp_ui_home_loaded(&state);
  }
}

void HomeActivity::onEnter() {
  Activity::onEnter();

  const bool hasOpdsServers = OPDS_STORE.hasServers();
  // Plugin System is the global opt-in. Once enabled, expose the Plugins entry
  // even when no SD plugins are installed yet so Plugin Hub can be installed
  // from inside the Plugins screen (or users can sideload plugins manually).
  const bool hasPlugins = SETTINGS.pluginsEnabled != 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  if (UITheme::getInstance().hasCoverGridHome()) {
    // Cover paths, font bindings and the cache belong to this screen.
    coverGridUi = makeUniqueNoThrow<CoverGridHomeUi>(renderer);
    if (!coverGridUi) LOG_ERR("HOME", "OOM: cover grid UI; using standard home");
  }
  loadRecentBooks(coverGridUi ? CoverGridHomeUi::MAX_BOOKS : metrics.homeRecentBooksCount);
  const bool hasContinueReading = !recentBooks.empty();
  if (coverGridUi) {
    fillCoverGridFromLibrary();
    resolveGridCoverPaths();
    coverGridUi->begin(recentBooks, hasContinueReading);
  }

  xp_ui_home_init(&state, recentBooks.size(), coverGridUi != nullptr, hasOpdsServers, hasPlugins, hasContinueReading,
                  static_cast<int>(initialMenuItem));

  // Trigger first update
  requestUpdate();
}

void HomeActivity::onExit() {
  Activity::onExit();

  coverGridUi.reset();

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::allocateCoverBuffer() {
  // render() must have already set the cover rect; without it we'd be back to
  // cloning the whole framebuffer.
  if (coverRectW <= 0 || coverRectH <= 0) return false;
  freeCoverBuffer();
  const size_t needed = renderer.getRegionByteSize(coverRectX, coverRectY, coverRectW, coverRectH);
  if (needed == 0) return false;
  coverBuffer = static_cast<uint8_t*>(malloc(needed));
  if (!coverBuffer) {
    LOG_ERR("HOME", "OOM: cover buffer (%u bytes)", (unsigned)needed);
    return false;
  }
  coverBufferSize = needed;
  return true;
}

bool HomeActivity::storeCoverBuffer() {
  if (!renderer.copyRegionToBuffer(coverRectX, coverRectY, coverRectW, coverRectH, coverBuffer, coverBufferSize)) {
    free(coverBuffer);
    coverBuffer = nullptr;
    coverBufferSize = 0;
    return false;
  }
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferSize = 0;
  coverBufferStored = false;
}

void HomeActivity::onRecentBookSelected(const std::string& path) { activityManager.goToReader(path); }

void HomeActivity::onFileBrowserOpen() { activityManager.goToFileBrowser(); }

void HomeActivity::onLibraryOpen() { activityManager.goToLibrary(); }

void HomeActivity::onSettingsOpen() { activityManager.goToSettings(); }

void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }

void HomeActivity::onOpdsBrowserOpen() { activityManager.goToBrowser(); }

void HomeActivity::onPluginsOpen() { activityManager.goToPlugins(state.opds); }
