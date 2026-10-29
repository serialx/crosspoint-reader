#include "EndOfBookOptions.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderChromeListActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/NextBookFinder.h"

namespace fui = freeink::ui;

EndOfBookOptions::EndOfBookOptions(GfxRenderer& renderer) : renderer(renderer), textTarget(makeUiTarget(renderer)) {
  xp_ui_reader_list_init(&state, 0);
}

void EndOfBookOptions::loadOnce(const std::string& currentBookPath) {
  if (isLoaded.load(std::memory_order_acquire)) return;
  folder = FsHelpers::extractFolderPath(currentBookPath);
  names = NextBookFinder::findNextBooks(currentBookPath, MAX_SUGGESTIONS);
  for (const auto& name : names) rowLabels[rowCount++] = name.substr(0, name.rfind('.'));
  if (!names.empty()) rowLabels[rowCount++] = tr(STR_EOB_HOME);
  isLoaded.store(true, std::memory_order_release);
}

bool EndOfBookOptions::menuActive() const { return isLoaded.load(std::memory_order_acquire) && !names.empty(); }

std::string EndOfBookOptions::fullPath(size_t index) const {
  if (index >= names.size()) return {};
  return folder == "/" ? "/" + names[index] : folder + "/" + names[index];
}

EndOfBookOptions::Action EndOfBookOptions::handleMenuInput(const MappedInputManager& input, std::string* openPath) {
  XpUiInput frame{};
  const MappedInputManager::Button buttons[] = {MappedInputManager::Button::Back, MappedInputManager::Button::Confirm,
                                                MappedInputManager::Button::NavNext,
                                                MappedInputManager::Button::NavPrevious};
  for (unsigned i = 0; i < 4; ++i) {
    if (input.wasPressed(buttons[i])) frame.pressed |= 1u << i;
    if (input.isPressed(buttons[i])) frame.held |= 1u << i;
    if (input.wasReleased(buttons[i])) frame.released |= 1u << i;
  }
  frame.held_ms = input.getHeldTime();
  const auto touch = touchSnapshotFrom(input);
  frame.touch_flags = (touch.touchPressed ? XP_UI_TOUCH_PRESSED : 0) | (touch.touchReleased ? XP_UI_TOUCH_RELEASED : 0);
  if (touch.touchHeld) frame.touch_flags |= XP_UI_TOUCH_HELD;
  frame.touch_x = touch.touchX;
  frame.touch_y = touch.touchY;
  taskENTER_CRITICAL(&mutex);
  const bool failed = sceneFailed;
  const bool heldOver =
      (suppressed & (frame.held | frame.released | frame.pressed)) != 0 || (suppressedContact && frame.touch_flags);
  if (failed) {
    suppressed |= frame.held;
    suppressedContact |= (frame.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD)) != 0;
  }
  suppressed &= frame.held;
  lastHeld = frame.held;
  lastContact = (frame.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD)) != 0;
  if (!lastContact) suppressedContact = false;
  if (failed || heldOver) {
    taskEXIT_CRITICAL(&mutex);
    if ((frame.released & XP_UI_BACK) && frame.held_ms < 1000) return Action::LastPage;
    // Redraw consumes activation while retaining the parent's long-Back route.
    if (frame.released & XP_UI_BACK) return Action::None;
    return Action::Redraw;
  }
  if (ready && frame.touch_flags) {
    XpUiInteraction hit{};
    if (xp_ui_reader_hit(&published, frame.touch_x, frame.touch_y, &hit)) {
      frame.action = hit.action;
      frame.value = hit.value;
    }
  }
  const auto effect = xp_ui_reader_end_update(&state.list, &frame, rowCount,
                                              SETTINGS.longPressButtonBehavior == CrossPointSettings::OFF);
  taskEXIT_CRITICAL(&mutex);
  const auto action = static_cast<Action>(effect.kind);
  if (frame.action && touch.touchReleased && (action == Action::OpenBook || action == Action::GoHome))
    haptic_feedback::touchAction(false);
  if (action == Action::OpenBook && openPath) *openPath = fullPath(effect.value);
  return action;
}

const char* EndOfBookOptions::text(uint32_t handle) const {
  if (handle == XP_UI_READER_TITLE) return tr(STR_END_OF_BOOK);
  if (handle == XP_UI_READER_SUBTITLE) return tr(STR_EOB_CONTINUE_WITH);
  if (handle > 0 && handle <= rowCount) return rowLabels[handle - 1].c_str();
  return nullptr;
}

XpUiReaderRows EndOfBookOptions::resources() {
  return {
      this,
      [](void* p, uint16_t first, uint16_t count) -> uint8_t {
        return first + count <= static_cast<EndOfBookOptions*>(p)->rowCount;
      },
      [](void* p, uint16_t index, XpUiReaderRow* row) -> uint8_t {
        if (index >= static_cast<EndOfBookOptions*>(p)->rowCount) return false;
        *row = {static_cast<uint32_t>(index) + 1, 0, 0, 0, 0, 0, {0, 0}};
        return true;
      },
      [](void* p, uint32_t handle, int32_t font, uint8_t bold) -> int32_t {
        auto& self = *static_cast<EndOfBookOptions*>(p);
        const char* value = self.text(handle);
        return value ? self.renderer.getTextWidth(font, value, static_cast<EpdFontFamily::Style>(bold)) : -1;
      },
      [](void* p, uint32_t handle, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
        auto& self = *static_cast<EndOfBookOptions*>(p);
        const char* value = self.text(handle);
        if (!value) return -1;
        self.textTarget.setFont(0, font);
        fui::TextStyle style;
        style.font = 0;
        style.bold = bold;
        style.maxLines = lines;
        return fui::measureWrappedText(self.textTarget, value, style, width).height;
      },
      [](void* p, int32_t font) -> int32_t { return static_cast<EndOfBookOptions*>(p)->renderer.getLineHeight(font); },
      rowCount,
      MAX_ROWS,
      1};
}

void EndOfBookOptions::render(GfxRenderer& renderer, const MappedInputManager& input) {
  XpUiReaderListState snapshot{};
  taskENTER_CRITICAL(&mutex);
  snapshot = state;
  taskEXIT_CRITICAL(&mutex);
  sampleReaderChromeTheme(renderer, theme);
  const auto rows = resources();
  const XpUiBuffer buffer{commands, nullptr, 16, 0};
  XpUiScene scene{};
  XpUiReaderHitLayout nextHits{};
  if (xp_ui_reader_list_view(&snapshot, XP_UI_READER_END, &theme, &rows, &buffer, &next, &scene, &nextHits) !=
      XP_UI_READER_VIEW_OK) {
    taskENTER_CRITICAL(&mutex);
    ready = false;
    sceneFailed = true;
    suppressed |= lastHeld;
    suppressedContact |= lastContact;
    taskEXIT_CRITICAL(&mutex);
    return;
  }
  renderingInput = &input;
  const XpUiPaint paint{
      this, nullptr, nullptr,
      [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
        return generation == 1 && (handle == XP_UI_READER_FOOTER || static_cast<EndOfBookOptions*>(p)->text(handle));
      },
      [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
        auto& self = *static_cast<EndOfBookOptions*>(p);
        if (command->resource == XP_UI_READER_FOOTER) {
          const auto labels =
              self.renderingInput->mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
          GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        } else
          self.renderer.drawUiText(*command, clip, self.text(command->resource));
      }};
  const bool valid = renderer.drawUiScene(scene, &paint);
  taskENTER_CRITICAL(&mutex);
  ready = valid;
  sceneFailed = !valid;
  if (!valid) {
    suppressed |= lastHeld;
    suppressedContact |= lastContact;
  }
  if (valid) {
    xp_ui_reader_list_publish(&state, &next, 0);
    published = nextHits;
  }
  taskEXIT_CRITICAL(&mutex);
}
