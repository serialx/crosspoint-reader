#include "ReaderChromeListActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

bool ReaderChromeCommands::reserve(uint16_t count) {
  if (commands && count <= capacity) return true;
  if (count > 128) return false;
  commands.reset();
  capacity = std::max(count, capacity);
  commands = makeUniqueNoThrow<XpUiCommand[]>(capacity);
  if (!commands) {
    LOG_ERR("RUI", "Cannot allocate %u command bytes", static_cast<unsigned>(capacity * sizeof(XpUiCommand)));
    return false;
  }
  return true;
}

bool ReaderChromeCommands::grow() { return capacity < 128 && reserve(std::min<uint16_t>(128, capacity + 16)); }

ReaderChromeListActivity::ReaderChromeListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                                                   XpUiReaderListKind kind, uint16_t rowCapacity)
    : RustActivityAdapter(name, renderer, input),
      kind(kind),
      rowCapacity(rowCapacity),
      textTarget(makeUiTarget(renderer)) {}

void ReaderChromeListActivity::onEnter() {
  Activity::onEnter();
  publication.ready = false;
  xp_ui_reader_list_init(&state, 0);
  const uint16_t initial = kind == XP_UI_READER_MENU ? 48 : kind == XP_UI_READER_BOOKMARKS ? 40 : 32;
  if (!arena.commands.reserve(initial)) {
    onBackButton();
    return;
  }
  requestUpdate();
}

void ReaderChromeListActivity::onExit() {
  publication.ready = false;
  arena.commands.reset();
  Activity::onExit();
}

void ReaderChromeListActivity::loop() {
  if (handleCustomInput()) return;
  if (consumeRejectedInput()) return;
  const bool actions = kind == XP_UI_READER_BOOKMARKS || kind == XP_UI_READER_CLIPPINGS;
  const bool longPress = actions && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, 700);
  bool ready;
  {
    ModelLock lock(*this);
    ready = publication.ready;
  }
  if (kind == XP_UI_READER_BOOKMARKS) mappedInput.wasSwipe(&nativeTouchX, &nativeTouchY);
  const bool longTouch = actions && ready && mappedInput.wasScreenLongPress(nativeTouchX, nativeTouchY);
  {
    ModelLock lock(*this);
    nativeLongPress = longPress;
    nativeLongTouch = longTouch;
  }
  RustActivityAdapter::loop();
}

void ReaderChromeListActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void ReaderChromeListActivity::selectIndex(int index) {
  ModelLock lock(*this);
  xp_ui_reader_list_select(&state, index, listCount());
}

XpUiInterest ReaderChromeListActivity::rustInterest(uint32_t now) const {
  auto interest = xp_ui_reader_list_interest(&state, kind, now);
  if (nativeLongPress || nativeLongTouch) {
    interest.held = 0;
    interest.deadline_enabled = 1;
    interest.deadline_ms = now;
  }
  return interest;
}

XpUiEffect ReaderChromeListActivity::updateRust(const XpUiInput& source) {
  XpUiInput input = source;
  touchActivated = false;
  if (publication.ready && input.touch_flags) {
    XpUiInteraction hit{};
    if (xp_ui_reader_hit(&publishedHits, input.touch_x, input.touch_y, &hit)) {
      input.action = hit.action;
      input.value = hit.value;
    }
  }
  bool longPress = nativeLongPress;
  const bool actions = kind == XP_UI_READER_BOOKMARKS || kind == XP_UI_READER_CLIPPINGS;
  if (actions) {
    if (nativeLongTouch && publication.ready) {
      XpUiInteraction hit{};
      if (xp_ui_reader_hit(&publishedHits, nativeTouchX, nativeTouchY, &hit) && hit.action == 1) {
        input.action = hit.action;
        input.value = hit.value;
        longPress = true;
      } else
        nativeLongTouch = false;
    }
  }
  int32_t swipeRow = -1;
  if (kind == XP_UI_READER_BOOKMARKS && input.swipe == XP_UI_SWIPE_LEFT && publication.ready) {
    XpUiInteraction hit{};
    if (xp_ui_reader_hit(&publishedHits, nativeTouchX, nativeTouchY, &hit) && hit.action == 1) swipeRow = hit.value;
  }
  const auto effect = xp_ui_reader_list_update(&state, &input, listCount(), hasModel(), kind, longPress, swipeRow);
  touchActivated = input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) && effect.kind == XP_UI_ACTIVATE;
  return effect;
}

void ReaderChromeListActivity::applyRustEffect(const XpUiEffect& effect) {
  bool longTouch, touch;
  {
    ModelLock lock(*this);
    longTouch = nativeLongTouch;
    touch = touchActivated;
    touchActivated = false;
  }
  switch (effect.kind) {
    case XP_UI_EFFECT_NONE:
      if (longTouch && effect.invalidate) haptic_feedback::touchAction(true);
      break;
    case XP_UI_CANCEL:
      onBackButton();
      break;
    case XP_UI_ACTIVATE:
      if (touch) haptic_feedback::touchAction(false);
      activateIndex(effect.value);
      break;
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), static_cast<int>(effect.value), listCount());
      break;
    case XP_UI_READER_ACTIONS:
      if (longTouch) haptic_feedback::touchAction(true);
      onRowLongPress(effect.value);
      break;
    case XP_UI_READER_DELETE:
      haptic_feedback::touchAction(false);
      swipeDelete(effect.value);
      break;
  }
}

const char* ReaderChromeListActivity::text(uint32_t handle) const {
  switch (handle) {
    case XP_UI_READER_TITLE:
    case XP_UI_READER_HEADER:
      return headerTitle();
    case XP_UI_READER_EMPTY:
      return emptyText();
    case XP_UI_READER_HELP:
      return helpText();
    case XP_UI_READER_PROGRESS:
      return progressText();
    default:
      if (handle == 0 || handle >= XP_UI_READER_HEADER) return nullptr;
      const auto* row = rowAt((handle - 1) / 4);
      if (!row) return nullptr;
      switch ((handle - 1) % 4) {
        case 0:
          return row->label;
        case 1:
          return row->subtitle;
        case 2:
          return row->value;
        case 3:
          return nullptr;
      }
      return nullptr;
  }
}

XpUiReaderRows ReaderChromeListActivity::rows() {
  return {this,
          [](void* p, uint16_t first, uint16_t) -> uint8_t {
            static_cast<ReaderChromeListActivity*>(p)->prepareRows(first);
            return 1;
          },
          [](void* p, uint16_t index, XpUiReaderRow* out) -> uint8_t {
            const auto* row = static_cast<ReaderChromeListActivity*>(p)->rowAt(index);
            if (!row || !row->label) return 0;
            const uint32_t base = static_cast<uint32_t>(index) * 4 + 1;
            *out = {base,
                    row->subtitle ? base + 1 : 0,
                    row->value ? base + 2 : 0,
                    row->bookmarkIcon ? base + 3 : 0,
                    row->toggle,
                    row->checked,
                    {0, 0}};
            return 1;
          },
          [](void* p, uint32_t handle, int32_t font, uint8_t bold) -> int32_t {
            const auto& self = *static_cast<ReaderChromeListActivity*>(p);
            const char* value = self.text(handle);
            return value ? self.renderer.getTextWidth(font, value, static_cast<EpdFontFamily::Style>(bold)) : -1;
          },
          [](void* p, uint32_t handle, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
            auto& self = *static_cast<ReaderChromeListActivity*>(p);
            const char* value = self.text(handle);
            if (!value) return -1;
            self.textTarget.setFont(0, font);
            fui::TextStyle style;
            style.font = 0;
            style.bold = bold;
            style.maxLines = lines;
            return fui::measureWrappedText(self.textTarget, value, style, width).height;
          },
          [](void* p, int32_t font) -> int32_t {
            return static_cast<ReaderChromeListActivity*>(p)->renderer.getLineHeight(font);
          },
          static_cast<uint16_t>(listCount()),
          rowCapacity,
          1};
}

void sampleReaderChromeTheme(const GfxRenderer& renderer, XpUiReaderTheme& theme, bool bookmarks) {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const auto fonts = uiScaleSpec();
  theme = {};
  theme.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                  static_cast<int16_t>(renderer.getScreenHeight())};
  theme.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                static_cast<int16_t>(safe.height)};
  theme.body_font = fonts.bodyFontId;
  theme.small_font = fonts.smallFontId;
  theme.title_font = fonts.titleFontId;
  theme.fixed_title_font = UI_12_FONT_ID;
  theme.help_font = bookmarks ? SMALL_FONT_ID : UI_10_FONT_ID;
  theme.row_min = BoardConfig::hasTouch() ? 0 : m.listRowHeight;
  theme.row_gap = m.listRowGap;
  theme.row_inset = m.listInset;
  theme.row_padding = m.listSidePadding;
  theme.scroll_width = m.listScrollWidth;
  theme.top_padding = m.topPadding;
  theme.header_height = m.headerHeight;
  theme.spacing = m.verticalSpacing;
  theme.tab_height = m.tabBarHeight;
  theme.header_padding = m.headerSidePadding;
  theme.space_sm = std::max(4, renderer.getLineHeight(fonts.bodyFontId) / 6);
  theme.space_md = 8;
  theme.space_lg = 16;
  theme.control_radius = m.controlRadius;
  theme.sheet_radius = m.sheetRadius;
  theme.row_radius = m.listRowRadius;
  theme.selection = m.listSelectionStyle;
  theme.scroll_left = m.listScrollSide;
  theme.title_bold = m.listTitleBold;
  theme.touch = BoardConfig::hasTouch();
  theme.orientation = renderer.getOrientation();
}

bool ReaderChromeListActivity::buildRustScene(XpUiScene& scene) {
  if (!arena.commands.recover() || listCount() < 0 || listCount() > UINT16_MAX) return false;
  XpUiReaderListState snapshot{};
  {
    ModelLock lock(*this);
    snapshot = state;
    renderingScroll = snapshot.list.pending_scroll;
  }
  sampleReaderChromeTheme(renderer, arena.theme, kind == XP_UI_READER_BOOKMARKS);
  const auto resources = rows();
  for (;;) {
    const auto buffer = arena.commands.buffer();
    const auto status = xp_ui_reader_list_view(&snapshot, kind, &arena.theme, &resources, &buffer, &arena.next, &scene,
                                               &arena.nextHits);
    if (status == XP_UI_READER_VIEW_OK) return true;
    if (status != XP_UI_READER_VIEW_CAPACITY || !arena.commands.grow()) return false;
  }
}

void ReaderChromeListActivity::rustPublished() {
  xp_ui_reader_list_publish(&state, &arena.next, renderingScroll);
  publishedHits = arena.nextHits;
}

bool ReaderChromeListActivity::renderList(bool refresh) {
  XpUiScene scene{};
  if (!buildRustScene(scene)) {
    rejectRustScene();
    return false;
  }
  if (!refresh) scene.refresh = XP_UI_NONE;
  return paintRustScene(scene);
}

void ReaderChromeListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

XpUiPaint ReaderChromeListActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
            auto& self = *static_cast<ReaderChromeListActivity*>(p);
            if (generation != 1) return false;
            if (handle == XP_UI_READER_FOOTER || handle == XP_UI_READER_DELETE_ICON) return true;
            if (handle < XP_UI_READER_HEADER && handle > 0 && (handle - 1) % 4 == 3) {
              const auto* row = self.rowAt((handle - 1) / 4);
              return row && row->bookmarkIcon;
            }
            return self.text(handle) != nullptr;
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<ReaderChromeListActivity*>(p);
            const auto& r = command->rect;
            const Rect rect{r.x, r.y, r.w, r.h};
            switch (command->resource) {
              case XP_UI_READER_HEADER:
                GUI.drawHeader(self.renderer,
                               self.kind == XP_UI_READER_MENU || self.kind == XP_UI_READER_CHAPTERS
                                   ? UITheme::getInstance().getHeaderRect(self.renderer, true, false)
                                   : rect,
                               self.headerTitle());
                return;
              case XP_UI_READER_FOOTER:
                self.drawFooter();
                return;
              case XP_UI_READER_HELP:
                GUI.drawHelpText(self.renderer, rect, self.helpText());
                return;
            }
            if (command->kind == XP_UI_BITMAP) {
              const auto bitmap = command->resource == XP_UI_READER_DELETE_ICON ? fui::bitmapFromIcon(icon_trash_2_24)
                                                                                : listIconFor(UIIcon::Bookmark, 32);
              const auto previous = self.renderer.getClipRect();
              self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
              self.textTarget.bitmap({r.x, r.y, r.w, r.h}, bitmap, fui::BitmapMode::Contain,
                                     fui::Paint::solid(command->paint == 1 ? fui::Color::White : fui::Color::Black));
              self.renderer.setClipRect(previous[0], previous[1], previous[2], previous[3]);
            } else
              self.renderer.drawUiText(*command, clip, self.text(command->resource));
          }};
}

void ReaderChromeListActivity::rustSceneFailed() {
  nativeLongPress = nativeLongTouch = false;
  touchActivated = false;
  state.list.last_repeat = 0;
}
