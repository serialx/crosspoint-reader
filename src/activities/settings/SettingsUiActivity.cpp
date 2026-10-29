#include "SettingsUiActivity.h"

#include <I18n.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "components/UIScale.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

SettingsUiActivity::SettingsUiActivity(const char* name, GfxRenderer& renderer, MappedInputManager& input,
                                       XpUiSettingsKind kind)
    : RustActivityAdapter(name, renderer, input), kind(kind), textTarget(makeUiTarget(renderer)) {}

bool SettingsUiActivity::reserveCommands(uint16_t count) {
  if (count <= commandCapacity) return true;
  if (count > 128) return false;
  requestedCapacity = count;
  commands.reset();
  commandCapacity = 0;
  commands = makeUniqueNoThrow<XpUiCommand[]>(count);
  if (!commands) {
    LOG_ERR("RUI", "Cannot allocate %u command bytes", static_cast<unsigned>(count * sizeof(XpUiCommand)));
    return false;
  }
  commandCapacity = count;
  return true;
}

void SettingsUiActivity::onEnter() {
  Activity::onEnter();
  publication.ready = false;
  sceneFailed = false;
  xp_ui_settings_list_init(&state, 0);
  // Small fixed menus reserve their row/chrome budget; measured overflow grows below.
  uint16_t initialCommands = 48;
  switch (kind) {
    case XP_UI_SETTINGS_SYNC:
      initialCommands = 8;
      break;
    case XP_UI_SETTINGS_HOME_BUTTON:
      initialCommands = 16;
      break;
    case XP_UI_SETTINGS_REMAP:
    case XP_UI_SETTINGS_CLOCK:
      initialCommands = 24;
      break;
    case XP_UI_SETTINGS_ROOT:
    case XP_UI_SETTINGS_STATUS_BAR:
      initialCommands = 32;
      break;
    case XP_UI_SETTINGS_SECTION:
    case XP_UI_SETTINGS_ABOUT:
    case XP_UI_SETTINGS_BLE:
    case XP_UI_SETTINGS_LAYOUTS:
    case XP_UI_SETTINGS_LANGUAGE:
    case XP_UI_SETTINGS_TIMEZONE:
      break;
  }
  if (!reserveCommands(initialCommands)) {
    onBackButton();
    return;
  }
  if (kind != XP_UI_SETTINGS_SYNC) requestUpdate();
}

void SettingsUiActivity::onExit() {
  publication.ready = false;
  commands.reset();
  commandCapacity = requestedCapacity = 0;
  Activity::onExit();
}

bool SettingsUiActivity::routingReady() const {
  ModelLock lock(*const_cast<SettingsUiActivity*>(this));
  return publication.ready;
}
int SettingsUiActivity::selectedIndex() const {
  ModelLock lock(*const_cast<SettingsUiActivity*>(this));
  return state.selected;
}
void SettingsUiActivity::selectIndex(int value) {
  ModelLock lock(*this);
  xp_ui_settings_list_select(&state, value, listCount());
}
void SettingsUiActivity::resetSelection(int value) {
  ModelLock lock(*this);
  xp_ui_settings_list_init(&state, value);
}
void SettingsUiActivity::resetUi() {
  ModelLock lock(*this);
  publication.ready = false;
}
void SettingsUiActivity::setCheckboxRow(SettingsUiRow& row, bool checked) {
  row.toggle = true;
  row.toggleChecked = checked;
  row.value = nullptr;
}

void SettingsUiActivity::loop() {
  if (handleCustomInput()) return;
  longTouch = kind == XP_UI_SETTINGS_BLE && routingReady() && mappedInput.wasScreenLongPress(touchX, touchY);
  RustActivityAdapter::loop();
}

XpUiInterest SettingsUiActivity::rustInterest(uint32_t now) const {
  auto interest = xp_ui_settings_list_interest(&state, now);
  if (longTouch) {
    interest.held = 0;
    interest.deadline_enabled = 1;
    interest.deadline_ms = now;
  }
  return interest;
}

XpUiEffect SettingsUiActivity::updateRust(const XpUiInput& source) {
  XpUiInput input = source;
  touchActivated = false;
  if (sceneFailed) {
    input.pressed &= XP_UI_BACK;
    input.released &= XP_UI_BACK;
    input.held = 0;
    input.action = 0;
    input.touch_flags = 0;
    input.swipe = 0;
    auto effect = xp_ui_settings_list_update(&state, &input, listCount());
    effect.invalidate = 1;
    return effect;
  }
  if (publication.ready && input.touch_flags) {
    XpUiInteraction hit{};
    if (xp_ui_settings_hit(&publishedHits, input.touch_x, input.touch_y, &hit)) {
      input.action = hit.action;
      input.value = hit.value;
    }
  }
  if (longTouch) {
    XpUiInteraction hit{};
    if (!publication.ready || !xp_ui_settings_hit(&publishedHits, touchX, touchY, &hit)) return {};
    input.action = 2;
    input.value = hit.value;
  }
  const auto effect = xp_ui_settings_list_update(&state, &input, listCount());
  touchActivated = input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) && effect.kind == XP_UI_ACTIVATE;
  return effect;
}

void SettingsUiActivity::applyRustEffect(const XpUiEffect& effect) {
  switch (effect.kind) {
    case XP_UI_CANCEL:
      onBackButton();
      break;
    case XP_UI_ACTIVATE:
      if (touchActivated) haptic_feedback::touchAction(false);
      activateIndex(effect.value);
      break;
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), static_cast<int>(effect.value), listCount());
      break;
    case 4:
      if (longTouch) haptic_feedback::touchAction(true);
      onRowLongPress(effect.value);
      break;
    default:
      break;
  }
}
void SettingsUiActivity::onBackButton() { finish(); }
void SettingsUiActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

const char* SettingsUiActivity::text(uint32_t handle) const {
  if (handle == 0 || handle >= 0x80000000u) return nullptr;
  const auto* row = rowAt((handle - 1) / 4);
  if (!row) return nullptr;
  switch ((handle - 1) % 4) {
    case 0:
      return row->label;
    case 1:
      return row->subtitle;
    case 2:
      return row->value;
    default:
      return nullptr;
  }
}

XpUiSettingsResources SettingsUiActivity::resources() {
  return {this,
          [](void* p, uint16_t index, XpUiSettingsRow* out) -> uint8_t {
            const auto* row = static_cast<SettingsUiActivity*>(p)->rowAt(index);
            if (!row || !row->label) return 0;
            *out = {row->label, row->subtitle, row->value, row->icon, row->toggle, row->toggleChecked, row->enabled, 0};
            return 1;
          },
          [](void* p, const char* value, int32_t font, uint8_t bold) -> int32_t {
            return static_cast<SettingsUiActivity*>(p)->renderer.getTextWidth(font, value,
                                                                              static_cast<EpdFontFamily::Style>(bold));
          },
          [](void* p, const char* value, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
            auto& self = *static_cast<SettingsUiActivity*>(p);
            self.textTarget.setFont(0, font);
            fui::TextStyle style;
            style.font = 0;
            style.bold = bold;
            style.maxLines = lines;
            return fui::measureWrappedText(self.textTarget, value, style, width).height;
          },
          [](void* p, int32_t font) -> int32_t {
            return static_cast<SettingsUiActivity*>(p)->renderer.getLineHeight(font);
          },
          static_cast<uint16_t>(listCount()),
          0};
}

bool SettingsUiActivity::buildRustScene(XpUiScene& scene) {
  const auto failed = [this]() {
    ModelLock lock(*this);
    sceneFailed = true;
    return false;
  };
  if ((!commands && !reserveCommands(requestedCapacity)) || listCount() < 0 || listCount() > UINT16_MAX)
    return failed();
  prepareRows();
  XpUiListState snapshot{};
  {
    ModelLock lock(*this);
    snapshot = state;
    renderingScroll = state.pending_scroll;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = textTarget.deviceContext();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  layout = {};
  auto& style = layout.list;
  style.screen = {0, 0, static_cast<int16_t>(renderer.getScreenWidth()),
                  static_cast<int16_t>(renderer.getScreenHeight())};
  style.area = {device.safeArea.left, device.safeArea.top,
                static_cast<int16_t>(device.width - device.safeArea.left - device.safeArea.right),
                static_cast<int16_t>(device.height - device.safeArea.top - device.safeArea.bottom)};
  style.font = fonts.bodyFontId;
  style.row_height = mappedInput.hasTouch() ? 0 : m.listRowHeight;
  style.row_gap = m.listRowGap;
  style.inset = m.listInset;
  style.padding = m.listSidePadding;
  style.scroll_width = m.listScrollWidth;
  style.radius = m.listRowRadius;
  style.selection = m.listSelectionStyle;
  style.scroll_left = m.listScrollSide;
  style.bold = m.listTitleBold;
  layout.safe = {static_cast<int16_t>(safe.x), static_cast<int16_t>(safe.y), static_cast<int16_t>(safe.width),
                 static_cast<int16_t>(safe.height)};
  layout.small_font = fonts.smallFontId;
  layout.fixed_font = UI_12_FONT_ID;
  layout.help_font = UI_10_FONT_ID;
  layout.top_padding = m.topPadding;
  layout.header_height = m.headerHeight;
  layout.hints_height = m.buttonHintsHeight;
  layout.spacing = m.verticalSpacing;
  layout.tab_height = m.tabBarHeight;
  layout.preview_height = UITheme::getInstance().getStatusBarHeight();
  layout.side_padding = m.contentSidePadding;
  layout.flags = screenFlags() | (smallLabelsOnTouch() ? XP_UI_SETTINGS_SMALL_LABELS_ON_TOUCH : 0);
  layout.kind = kind;
  layout.touch = mappedInput.hasTouch();
  const auto services = resources();
  for (;;) {
    const XpUiBuffer buffer{commands.get(), nullptr, commandCapacity, 0};
    const auto result = xp_ui_settings_view(&snapshot, &layout, &services, &buffer, &next, &scene, &nextHits);
    if (result == 1) return true;
    if (result != 2 || !reserveCommands(commandCapacity + 16)) return failed();
  }
}
void SettingsUiActivity::rustPublished() {
  sceneFailed = false;
  xp_ui_settings_list_publish(&state, &next, renderingScroll);
  publishedHits = nextHits;
}
XpUiPaint SettingsUiActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* p, uint32_t handle, uint32_t generation) -> uint8_t {
            if (generation != 1) return false;
            if (handle >= 0x80000001u && handle <= 0x80000007u) return true;
            const auto& self = *static_cast<SettingsUiActivity*>(p);
            if (handle > 0 && handle < 0x80000000u && (handle - 1) % 4 == 3) {
              const auto* row = self.rowAt((handle - 1) / 4);
              return row && row->icon > 0 && row->icon <= 6;
            }
            return self.text(handle) != nullptr;
          },
          [](void* p, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<SettingsUiActivity*>(p);
            const auto r = command->rect;
            switch (command->resource) {
              case 0x80000001u:
                GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, self.headerTitle(), self.headerTrailing());
                return;
              case 0x80000002u:
                self.drawFooter();
                return;
              case 0x80000003u:
                self.drawPreview();
                return;
              case 0x80000004u:
                GUI.drawSubHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, self.specialText(command->resource));
                return;
              case 0x80000005u:
              case 0x80000006u:
              case 0x80000007u:
                GUI.drawHelpText(self.renderer, Rect{r.x, r.y, r.w, r.h}, self.specialText(command->resource));
                return;
            }
            if (command->kind == XP_UI_BITMAP) {
              static constexpr const freeink::Icon* icons[] = {&icon_settings_32,  &icon_smartphone_32,
                                                               &icon_book_open_32, &icon_pointer_32,
                                                               &icon_library_32,   &icon_wifi_32};
              const auto* row = self.rowAt((command->resource - 1) / 4);
              const auto old = self.renderer.getClipRect();
              self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
              self.textTarget.bitmap({r.x, r.y, r.w, r.h}, fui::bitmapFromIcon(*icons[row->icon - 1]),
                                     fui::BitmapMode::Contain,
                                     fui::Paint::solid(command->paint == 1 ? fui::Color::White : fui::Color::Black));
              self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
            } else
              self.renderer.drawUiText(*command, clip, self.text(command->resource));
          }};
}
