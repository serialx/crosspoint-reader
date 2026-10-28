#include "XtcReaderChapterSelectionActivity.h"

#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "components/UiAppHelpers.h"
#include "components/themes/BaseTheme.h"
#include "fontIds.h"

XtcReaderChapterSelectionActivity::XtcReaderChapterSelectionActivity(GfxRenderer& renderer,
                                                                     MappedInputManager& mappedInput,
                                                                     const std::shared_ptr<Xtc>& xtc,
                                                                     const uint32_t currentPage)
    : RustActivityAdapter("XtcReaderChapterSelection", renderer, mappedInput), xtc(xtc), currentPage(currentPage) {}

const char* XtcReaderChapterSelectionActivity::text(const uint32_t handle) const {
  switch (handle) {
    case XP_UI_XTC_TITLE:
      return tr(STR_SELECT_CHAPTER);
    case XP_UI_XTC_EMPTY:
      return tr(STR_NO_CHAPTERS);
    default:
      if (handle >= XP_UI_XTC_FOOTER && handle < XP_UI_XTC_FOOTER + 4)
        return arena ? arena->hints[handle - XP_UI_XTC_FOOTER] : nullptr;
      if (xtc && handle > 0 && handle <= xtc->getChapters().size()) {
        const auto& name = xtc->getChapters()[handle - 1].name;
        return name.empty() ? tr(STR_UNNAMED) : name.c_str();
      }
      return nullptr;
  }
}

XpUiResources XtcReaderChapterSelectionActivity::resources() {
  return {this,
          [](void* p, uint32_t handle, uint16_t* length) -> const uint8_t* {
            const char* value = static_cast<XtcReaderChapterSelectionActivity*>(p)->text(handle);
            const size_t size = value ? strlen(value) : 0;
            *length = size < XP_UI_MAX_TEXT ? static_cast<uint16_t>(size) : 0;
            return size < XP_UI_MAX_TEXT ? reinterpret_cast<const uint8_t*>(value) : nullptr;
          },
          [](void* p, uint16_t index, uint32_t* start, uint32_t* end) {
            const auto& book = static_cast<XtcReaderChapterSelectionActivity*>(p)->xtc;
            const auto& chapter = book->getChapters()[index];
            *start = chapter.startPage;
            *end = chapter.endPage;
          },
          static_cast<uint16_t>(xtc ? xtc->getChapters().size() : 0),
          static_cast<uint8_t>(!!xtc),
          0,
          1};
}

void XtcReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();
  publication.ready = false;
  arena = makeUniqueNoThrow<Arena>();
  if (!arena || (xtc && xtc->getChapters().size() > UINT16_MAX)) {
    LOG_ERR("RUI", "Cannot allocate chapter scene");
    applyRustEffect(XpUiEffect{0, XP_UI_CANCEL, 0, {0, 0}});
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  arena->hints[0] = labels.btn1;
  arena->hints[1] = labels.btn2;
  arena->hints[2] = labels.btn3;
  arena->hints[3] = labels.btn4;
  const auto target = makeUiTarget(renderer);
  refreshSharedUiThemeTokens(target);
  const auto book = resources();
  xp_ui_list_init(&state, &book, currentPage);
  requestUpdate();
}

void XtcReaderChapterSelectionActivity::onExit() {
  publication.ready = false;
  arena.reset();
  Activity::onExit();
}

XpUiPaint XtcReaderChapterSelectionActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
            const char* value = static_cast<XtcReaderChapterSelectionActivity*>(p)->text(handle);
            return generation == 1 && value && strlen(value) < XP_UI_MAX_TEXT;
          },
          [](void* p, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<XtcReaderChapterSelectionActivity*>(p);
            const char* value = self.text(command->resource);
            if (command->kind == XP_UI_HINT_TEXT) {
              const auto old = self.renderer.getClipRect();
              self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
              BaseTheme::drawHintLabel(self.renderer, command->font, value, command->rect.x, command->rect.w,
                                       command->rect.y, command->rect.h);
              self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
            } else {
              self.renderer.drawUiText(*command, clip, value);
            }
          }};
}

bool XtcReaderChapterSelectionActivity::buildRustScene(XpUiScene& scene) {
  if (!arena) return false;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& tokens = *sharedUiThemeCell().load(std::memory_order_acquire);
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto scale = uiScaleSpec();
  const bool touch = BoardConfig::hasTouch();
  XpUiXtcLayout layout{};
  layout.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                   static_cast<int16_t>(renderer.getScreenHeight())};
  layout.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                 static_cast<int16_t>(safe.height)};
  layout.list.screen = layout.screen;
  layout.list.min_touch = tokens.minTouchSize;
  layout.list.font = scale.bodyFontId;
  layout.list.row_height = std::max<int>(
      renderer.getLineHeight(scale.bodyFontId) + 2 * (touch ? tokens.listTouchRowPaddingY : tokens.listRowPaddingY),
      tokens.listMinRowHeight);
  if (touch)
    layout.list.row_height = std::max({layout.list.row_height, tokens.minTouchSize, tokens.listTouchMinRowHeight});
  layout.list.row_gap = touch ? std::max(tokens.listRowGap, tokens.listTouchRowGap) : tokens.listRowGap;
  layout.list.inset = tokens.listInset;
  layout.list.padding = tokens.listSidePadding;
  layout.list.scroll_width = tokens.listScrollWidth;
  layout.list.radius = tokens.listRowRadius;
  layout.list.selection = static_cast<uint8_t>(tokens.listSelectionStyle);
  layout.list.scroll_left = tokens.listScrollSide;
  layout.list.bold = tokens.bodyText.bold;
  layout.title_font = UI_12_FONT_ID;
  layout.hint_font = SETTINGS.uiTheme == CrossPointSettings::CLASSIC ? UI_10_FONT_ID : SMALL_FONT_ID;
  layout.top_padding = metrics.topPadding;
  layout.header_height = metrics.headerHeight;
  layout.spacing = metrics.verticalSpacing;
  switch (static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme)) {
    case CrossPointSettings::CLASSIC:
      layout.footer_style = 0;
      break;
    case CrossPointSettings::ROUNDEDRAFF:
      layout.footer_style = 2;
      break;
    case CrossPointSettings::LYRA:
    case CrossPointSettings::LYRA_3_COVERS:
    case CrossPointSettings::COVER_GRID:
      layout.footer_style = 1;
      break;
  }
  layout.touch = touch;
  const auto book = resources();
  const auto fonts = rustMetrics();
  const XpUiBuffer buffer{arena->commands, arena->hits, 80, 32};
  return xp_ui_xtc_view(&state, &layout, &fonts, &book, &buffer, &arena->nextState, &scene) != 0;
}

XpUiEffect XtcReaderChapterSelectionActivity::updateRust(const XpUiInput& input) {
  return xp_ui_list_update(&state, &input, xtc ? xtc->getChapters().size() : 0, !!xtc);
}

void XtcReaderChapterSelectionActivity::applyRustEffect(const XpUiEffect& effect) {
  switch (static_cast<XpUiEffectKind>(effect.kind)) {
    case XP_UI_CANCEL: {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
      break;
    }
    case XP_UI_ACTIVATE:
      if (xtc && effect.value >= 0 && static_cast<size_t>(effect.value) < xtc->getChapters().size()) {
        setResult(PageResult{xtc->getChapters()[effect.value].startPage});
        finish();
      }
      break;
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), static_cast<int>(effect.value),
              xtc ? static_cast<int>(xtc->getChapters().size()) : 0);
      break;
    case XP_UI_EFFECT_NONE:
      break;
  }
}
