#include "ReaderToolbarUi.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "ReaderChromeListActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/readerToolbarIcons.h"

namespace fui = freeink::ui;

ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& renderer) : renderer(renderer), textTarget(makeUiTarget(renderer)) {}

void ReaderToolbarUi::begin() {
  taskENTER_CRITICAL(&mutex);
  ready = false;
  contact = {-1, -1, 0, {0, 0, 0}};
  xp_ui_reader_toolbar_nav_reset(&nav_);
  taskEXIT_CRITICAL(&mutex);
}

void ReaderToolbarUi::resetNavigation(const int selected, const int top) {
  taskENTER_CRITICAL(&mutex);
  xp_ui_reader_toolbar_nav_reset(&nav_);
  nav_.selected = selected;
  nav_.top = top;
  taskEXIT_CRITICAL(&mutex);
}

bool ReaderToolbarUi::scrollNavigation(const int delta, const int count) {
  taskENTER_CRITICAL(&mutex);
  const bool moved = xp_ui_reader_toolbar_scroll(&nav_, delta, count);
  taskEXIT_CRITICAL(&mutex);
  return moved;
}

int ReaderToolbarUi::visibleRows() const {
  taskENTER_CRITICAL(&mutex);
  const int rows = xp_ui_reader_toolbar_page_rows(&nav_);
  taskEXIT_CRITICAL(&mutex);
  return rows;
}

ReaderToolbarUi::Routed ReaderToolbarUi::route(const MappedInputManager& input) {
  Routed result{};
  taskENTER_CRITICAL(&mutex);
  const bool available = ready;
  taskEXIT_CRITICAL(&mutex);
  if (!available) return result;
  const auto touch = touchSnapshotFrom(input);
  const XpUiInput frame{
      0,
      0,
      0,
      0,
      0,
      touch.touchX,
      touch.touchY,
      static_cast<uint8_t>((touch.touchPressed ? XP_UI_TOUCH_PRESSED : 0) |
                           (touch.touchReleased ? XP_UI_TOUCH_RELEASED : 0) | (touch.touchHeld ? XP_UI_TOUCH_HELD : 0)),
      0,
      0,
      0,
      0};
  int32_t permille = -1;
  taskENTER_CRITICAL(&mutex);
  // cppcheck-suppress knownConditionTrueFalse // Another task can clear ready between the two lock scopes.
  const auto effect = ready ? xp_ui_reader_toolbar_route(&contact, &published, &frame, &permille) : XpUiEffect{};
  taskEXIT_CRITICAL(&mutex);
  result.permille = permille;
  result.event = static_cast<Event>(effect.kind);
  result.value = effect.value;
  result.routed = effect.invalidate;
  result.x = touch.touchX;
  result.y = touch.touchY;
  if (result.event != Event::None && touch.touchReleased) haptic_feedback::touchAction(false);
  return result;
}

const char* ReaderToolbarUi::text(uint32_t handle) const {
  switch (handle) {
    case XP_UI_READER_TITLE:
      return model_.chapterTitle;
    case XP_UI_READER_PROGRESS:
      return model_.pageInfo;
    case XP_UI_READER_HEADER:
      return model_.panelTitle;
    case XP_UI_READER_PANEL_PAGE:
      return pageIndicator;
  }
  if (handle == 0 || handle >= XP_UI_READER_HEADER) return nullptr;
  const uint32_t index = (handle - 1) / 2;
  if (index < windowFirst || index >= windowFirst + windowCount) return nullptr;
  return (handle - 1) % 2 == 0 ? windowLabels_[index - windowFirst].c_str()
                               : windowValues_[index - windowFirst].c_str();
}

XpUiReaderRows ReaderToolbarUi::resources() {
  return {
      this,
      [](void* p, uint16_t first, uint16_t count) -> uint8_t {
        auto& self = *static_cast<ReaderToolbarUi*>(p);
        if (count > kMaxWindow || static_cast<int>(first) + count > self.model_.itemCount) return false;
        self.windowFirst = first;
        self.windowCount = count;
        for (uint16_t i = 0; i < count; ++i) {
          const int index = first + i;
          self.windowLabels_[i] = self.model_.rowText ? self.model_.rowText(index) : std::string();
          self.windowValues_[i] = self.model_.rowValue ? self.model_.rowValue(index) : std::string();
          self.rowFlags[i] =
              self.model_.rowCheckbox ? self.model_.rowCheckbox(self.model_.rowCheckboxContext, index) : RowFlags{};
        }
        return true;
      },
      [](void* p, uint16_t index, XpUiReaderRow* row) -> uint8_t {
        const auto& self = *static_cast<ReaderToolbarUi*>(p);
        if (index < self.windowFirst || index >= self.windowFirst + self.windowCount) return false;
        const auto& flags = self.rowFlags[index - self.windowFirst];
        const uint32_t base = static_cast<uint32_t>(index) * 2 + 1;
        *row = {
            base,          0,     self.windowValues_[index - self.windowFirst].empty() ? 0 : base + 1, 0, flags.toggle,
            flags.checked, {0, 0}};
        return true;
      },
      [](void* p, uint32_t handle, int32_t font, uint8_t bold) -> int32_t {
        const auto& self = *static_cast<ReaderToolbarUi*>(p);
        const char* value = self.text(handle);
        return value ? self.renderer.getTextWidth(font, value, static_cast<EpdFontFamily::Style>(bold)) : -1;
      },
      [](void* p, uint32_t handle, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
        auto& self = *static_cast<ReaderToolbarUi*>(p);
        const char* value = self.text(handle);
        if (!value) return -1;
        self.textTarget.setFont(0, font);
        fui::TextStyle style;
        style.font = 0;
        style.bold = bold;
        style.maxLines = lines;
        return fui::measureWrappedText(self.textTarget, value, style, width).height;
      },
      [](void* p, int32_t font) -> int32_t { return static_cast<ReaderToolbarUi*>(p)->renderer.getLineHeight(font); },
      static_cast<uint16_t>(std::max(0, model_.itemCount)),
      kMaxWindow,
      1};
}

bool ReaderToolbarUi::render() {
  taskENTER_CRITICAL(&mutex);
  XpUiListState state = nav_;
  taskEXIT_CRITICAL(&mutex);
  state.selected = std::clamp(model_.selectedIndex, -1, std::max(0, model_.itemCount) - 1);
  state.active = -1;
  state.follow = state.selected >= 0;
  sampleReaderChromeTheme(renderer, theme);
  const auto safe = textTarget.deviceContext().safeRect();
  theme.safe = {safe.x, safe.y, safe.width, safe.height};
  theme.row_min = UITheme::getInstance().getMetrics().listRowHeight;
  const XpUiReaderToolbarModel model{model_.panel,
                                     model_.denseRows,
                                     {0, 0},
                                     model_.activeTool,
                                     model_.progressPermille,
                                     model_.chapterTitle ? XP_UI_READER_TITLE : 0,
                                     model_.pageInfo ? XP_UI_READER_PROGRESS : 0,
                                     model_.panelTitle ? XP_UI_READER_HEADER : 0};
  const auto rows = resources();
  XpUiListState next{};
  XpUiReaderHitLayout nextHits{};
  XpUiScene scene{};
  bool built = false;
  if (model_.itemCount <= UINT16_MAX && commands.reserve(24)) {
    for (;;) {
      const auto buffer = commands.buffer();
      const auto status = xp_ui_reader_toolbar_view(&state, &model, &theme, &rows, &buffer, &next, &scene, &nextHits);
      if (status == XP_UI_READER_VIEW_OK) {
        built = true;
        break;
      }
      if (status != XP_UI_READER_VIEW_CAPACITY || !commands.grow()) break;
    }
  }
  if (!built) {
    taskENTER_CRITICAL(&mutex);
    ready = false;
    contact = {-1, -1, 0, {0, 0, 0}};
    taskEXIT_CRITICAL(&mutex);
    return false;
  }
  const int pageRows = std::max<int32_t>(1, next.drawn > 0 ? next.drawn : next.visible);
  snprintf(pageIndicator, sizeof(pageIndicator), "%d/%d", static_cast<int>(next.top / pageRows + 1),
           (std::max(0, model_.itemCount) + pageRows - 1) / pageRows);
  const XpUiPaint paint{this, nullptr, nullptr,
                        [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
                          return generation == 1 &&
                                 ((handle >= XP_UI_READER_TOOL_CONTENTS && handle <= XP_UI_READER_TOOL_NEXT) ||
                                  static_cast<ReaderToolbarUi*>(p)->text(handle));
                        },
                        [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
                          auto& self = *static_cast<ReaderToolbarUi*>(p);
                          if (command->kind != XP_UI_BITMAP) {
                            self.renderer.drawUiText(*command, clip, self.text(command->resource));
                            return;
                          }
                          fui::BitmapRef bitmap;
                          switch (command->resource) {
                            case XP_UI_READER_TOOL_CONTENTS:
                              bitmap = fui::bitmapFromIcon(icon_reader_contents_24);
                              break;
                            case XP_UI_READER_TOOL_TEXT:
                              bitmap = fui::bitmapFromIcon(icon_reader_text_24);
                              break;
                            case XP_UI_READER_TOOL_MORE:
                              bitmap = fui::bitmapFromIcon(icon_reader_more_24);
                              break;
                            case XP_UI_READER_TOOL_PREV:
                              bitmap = fui::bitmapFromIcon(icon_reader_back_24);
                              break;
                            case XP_UI_READER_TOOL_NEXT:
                              bitmap = fui::bitmapFromIcon(icon_reader_next_24);
                              break;
                          }
                          const auto old = self.renderer.getClipRect();
                          self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
                          const auto& r = command->rect;
                          self.textTarget.bitmap({r.x, r.y, r.w, r.h}, bitmap, fui::BitmapMode::Center);
                          self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
                        }};
  const bool valid = renderer.drawUiScene(scene, &paint);
  taskENTER_CRITICAL(&mutex);
  ready = valid;
  if (!valid) contact = {-1, -1, 0, {0, 0, 0}};
  if (valid) {
    published = nextHits;
    xp_ui_reader_toolbar_nav_publish(&nav_, &next);
  }
  taskEXIT_CRITICAL(&mutex);
  return valid;
}
