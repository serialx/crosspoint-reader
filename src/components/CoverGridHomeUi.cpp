#include "CoverGridHomeUi.h"

#include <BoardConfig.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "UIScale.h"
#include "UITheme.h"
#include "icons/blocks.h"
#include "icons/book.h"
#include "icons/folder.h"
#include "icons/library.h"
#include "icons/settings2.h"
#include "icons/transfer.h"
#include "util/BookProgress.h"

void CoverGridHomeUi::begin(const std::vector<RecentBook>& recent, bool continuing) {
  books = &recent;
  if (!recent.empty()) coverCache.begin();
  refreshCoverPaths();
  progress = continuing && !recent.empty() ? loadBookProgress(recent.front().path) : -1;
  if (progress >= 0) snprintf(progressText, sizeof(progressText), "%d%%", progress);
}

void CoverGridHomeUi::refreshCoverPaths() {
  coverCache.invalidate();
  for (size_t i = 0; i < books->size() && i < coverPaths.size(); ++i) {
    coverCache.invalidate(i);
    coverPaths[i] = thumbHeight > 0 ? UITheme::getCoverThumbPath((*books)[i].coverBmpPath, thumbHeight) : std::string();
  }
}

bool CoverGridHomeUi::prepare(const XpUiHomeGridGeometry& geometry) {
  coverCache.prepare();
  const bool changed = geometry.thumb_height > 0 && thumbHeight != geometry.thumb_height;
  if (changed) {
    thumbHeight = geometry.thumb_height;
    refreshCoverPaths();
  }
  return changed;
}

XpUiHomeGridLayout CoverGridHomeUi::layout() const {
  XpUiHomeGridLayout l{};
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& board = BoardConfig::ACTIVE;
  const auto fonts = uiScaleSpec();
  l.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()), static_cast<int16_t>(renderer.getScreenHeight())};
  l.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
            static_cast<int16_t>(safe.height)};
  l.body_font = fonts.bodyFontId;
  l.small_font = fonts.smallFontId;
  l.title_font = fonts.titleFontId;
  l.progress = progress;
  l.space_sm = std::max(4, renderer.getLineHeight(fonts.bodyFontId) / 6);
  l.space_lg = 16;
  l.header_side = metrics.headerSidePadding;
  l.header_top = metrics.topPadding;
  l.header_height = metrics.batteryBarHeight;
  l.corner = board.cornerRadius;
  l.tab_height = metrics.coverGridTabBarHeight;
  l.tab_height_pct = board.tabBarHeightPct;
  l.tab_width_pct = board.tabBarWidthPct;
  l.tab_offset = board.tabBarOffsetY;
  l.touch = BoardConfig::hasTouch();
  l.body_bold = metrics.listTitleBold;
  return l;
}

const char* CoverGridHomeUi::text(uint32_t h) const {
  switch (h) {
    case XP_UI_HOME_TITLE:
      return books->empty() ? nullptr : books->front().title.c_str();
    case XP_UI_HOME_AUTHOR:
      return books->empty() ? nullptr : books->front().author.c_str();
    case XP_UI_HOME_NO_OPEN_BOOK:
      return tr(STR_NO_OPEN_BOOK);
    case XP_UI_HOME_START_READING:
      return tr(STR_START_READING);
    case XP_UI_HOME_PROGRESS:
      return progressText;
    default:
      return nullptr;
  }
}

XpUiHomeGridTexts CoverGridHomeUi::textServices() {
  return {this,
          [](void* p, uint32_t h, int32_t f) -> int32_t {
            const auto& self = *static_cast<CoverGridHomeUi*>(p);
            const char* s = self.text(h);
            return s ? self.renderer.getTextWidth(f, s) : -1;
          },
          [](void* p, int32_t f) -> int32_t { return static_cast<CoverGridHomeUi*>(p)->renderer.getLineHeight(f); },
          [](void* p, uint32_t h, int32_t f, int16_t width, uint8_t lines) -> int32_t {
            const auto& self = *static_cast<CoverGridHomeUi*>(p);
            const char* s = self.text(h);
            if (!s) return -1;
            freeink::ui::GfxRendererTarget target(self.renderer);
            target.setFont(0, f);
            freeink::ui::TextStyle style;
            style.maxLines = lines;
            style.bold = UITheme::getInstance().getMetrics().listTitleBold;
            return freeink::ui::measureWrappedText(target, s, style, width).height;
          },
          [](void* p, uint32_t h) -> uint8_t {
            const char* s = static_cast<CoverGridHomeUi*>(p)->text(h);
            return !s || !s[0];
          }};
}

void CoverGridHomeUi::paint(uint32_t h, XpUiRect r) {
  if (h >= XP_UI_HOME_GRID_COVER && h < XP_UI_HOME_GRID_COVER + books->size()) {
    coverCache.paint({r.x, r.y, r.w, r.h}, h - XP_UI_HOME_GRID_COVER, coverPaths[h - XP_UI_HOME_GRID_COVER]);
  } else if (h >= XP_UI_HOME_GRID_ICON && h < XP_UI_HOME_GRID_ICON + 6) {
    static constexpr const uint8_t* icons[] = {FolderIcon,   LibraryIcon,   BlocksIcon,
                                               TransferIcon, Settings2Icon, BookIcon};
    renderer.drawIcon(icons[h - XP_UI_HOME_GRID_ICON], r.x, r.y, r.w);
  }
}
