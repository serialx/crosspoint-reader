#include "NetworkModeSelectionActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UiAppHelpers.h"
namespace fui = freeink::ui;
namespace {
constexpr StrId menuItems[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    StrId::STR_JOIN_NETWORK,
    StrId::STR_CALIBRE_WIRELESS,
    StrId::STR_CREATE_HOTSPOT,
#if FREEINK_CAP_USB_MSC
    StrId::STR_USB_DRIVE,
#endif
};
constexpr StrId menuDescs[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    StrId::STR_JOIN_DESC,
    StrId::STR_CALIBRE_DESC,
    StrId::STR_HOTSPOT_DESC,
#if FREEINK_CAP_USB_MSC
    StrId::STR_USB_DRIVE_DESC,
#endif
};
constexpr UIIcon menuIcons[NetworkModeSelectionActivity::MENU_ITEM_COUNT] = {
    UIIcon::Wifi,
    UIIcon::Library,
    UIIcon::Hotspot,
#if FREEINK_CAP_USB_MSC
    UIIcon::Usb,
#endif
};
}  // namespace
NetworkModeSelectionActivity::NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& input)
    : RustActivityAdapter("NetworkModeSelection", renderer, input), textTarget(makeUiTarget(renderer)) {
  for (int i = 0; i < MENU_ITEM_COUNT; ++i)
    rows[i] = {I18N.get(menuItems[i]), I18N.get(menuDescs[i]), nullptr, static_cast<uint32_t>(i + 1), 0, 0, 1, 0};
}
void NetworkModeSelectionActivity::onEnter() {
  Activity::onEnter();
  xp_ui_settings_list_init(&state, 0);
  publication.ready = false;
  sceneFailed = false;
  requestUpdate();
}
void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}
void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
void NetworkModeSelectionActivity::loop() {
  uint32_t held = 0;
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) held |= XP_UI_CONFIRM;
  if (mappedInput.isPressed(MappedInputManager::Button::NavNext)) held |= XP_UI_NAV_NEXT;
  if (mappedInput.isPressed(MappedInputManager::Button::NavPrevious)) held |= XP_UI_NAV_PREVIOUS;
  const auto touch = touchSnapshotFrom(mappedInput);
  {
    ModelLock lock(*this);
    sampledHeld = held;
    sampledTouch = touch.touchPressed || touch.touchHeld;
    // The shared rejection gate can consume the release before updateRust runs.
    suppressedButtons &= held;
    if (!sampledTouch) suppressedTouch = false;
  }
  RustActivityAdapter::loop();
}
void NetworkModeSelectionActivity::rustSceneFailed() {
  sceneFailed = true;
  state.active = -1;
  state.last_repeat = 0;
  state.pending_scroll = 0;
  state.navigation_revision++;
  touchActivated = false;
  suppressedButtons |= sampledHeld;
  suppressedTouch |= sampledTouch;
}
XpUiInterest NetworkModeSelectionActivity::rustInterest(uint32_t now) const {
  auto interest = xp_ui_settings_list_interest(&state, now);
  if (sceneFailed || suppressedButtons || suppressedTouch) {
    interest.pressed |= XP_UI_CONFIRM | XP_UI_NAV_NEXT | XP_UI_NAV_PREVIOUS;
    interest.released |= XP_UI_CONFIRM | XP_UI_NAV_NEXT | XP_UI_NAV_PREVIOUS;
    interest.touch_flags |= XP_UI_TOUCH_HELD;
  }
  return interest;
}
XpUiEffect NetworkModeSelectionActivity::updateRust(const XpUiInput& source) {
  auto input = source;
  touchActivated = false;
  if (sceneFailed) {
    suppressedButtons |= (source.pressed | source.held) & (XP_UI_CONFIRM | XP_UI_NAV_NEXT | XP_UI_NAV_PREVIOUS);
    suppressedButtons &= ~source.released;
    suppressedTouch = (source.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD)) != 0;
    XpUiInput cancel{};
    cancel.released = source.released & XP_UI_BACK;
    auto effect = xp_ui_settings_list_update(&state, &cancel, 0);
    if (effect.kind != XP_UI_CANCEL) effect.invalidate = 1;
    return effect;
  }
  input.pressed &= ~suppressedButtons;
  input.released &= ~suppressedButtons;
  input.held &= ~suppressedButtons;
  suppressedButtons &= source.held;
  if (suppressedTouch) {
    input.touch_flags = 0;
    suppressedTouch = (source.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD)) != 0;
  }
  if (publication.ready && input.touch_flags) {
    XpUiInteraction hit{};
    if (xp_ui_settings_hit(&hits, input.touch_x, input.touch_y, &hit)) {
      input.action = hit.action;
      input.value = hit.value;
    }
  }
  const auto effect = xp_ui_settings_list_update(&state, &input, MENU_ITEM_COUNT);
  touchActivated = input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) && effect.kind == XP_UI_ACTIVATE;
  return effect;
}
void NetworkModeSelectionActivity::applyRustEffect(const XpUiEffect& effect) {
  switch (effect.kind) {
    case XP_UI_CANCEL:
      onCancel();
      break;
    case XP_UI_ACTIVATE:
      if (touchActivated) haptic_feedback::touchAction(false);
      onModeSelected(static_cast<NetworkMode>(effect.value));
      break;
    case XP_UI_SCROLL:
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), static_cast<int>(effect.value), MENU_ITEM_COUNT);
      break;
    default:
      break;
  }
}
const char* NetworkModeSelectionActivity::text(uint32_t handle) const {
  if (handle == 0 || (handle - 1) / 4 >= MENU_ITEM_COUNT) return nullptr;
  const auto& row = rows[(handle - 1) / 4];
  switch ((handle - 1) % 4) {
    case 0:
      return row.label;
    case 1:
      return row.subtitle;
    default:
      return nullptr;
  }
}
XpUiSettingsResources NetworkModeSelectionActivity::resources() {
  return {this,
          [](void* context, uint16_t index, XpUiSettingsRow* out) -> uint8_t {
            if (index >= MENU_ITEM_COUNT) return 0;
            *out = static_cast<NetworkModeSelectionActivity*>(context)->rows[index];
            return 1;
          },
          [](void* context, const char* value, int32_t font, uint8_t bold) -> int32_t {
            return static_cast<NetworkModeSelectionActivity*>(context)->renderer.getTextWidth(
                font, value, static_cast<EpdFontFamily::Style>(bold));
          },
          [](void* context, const char* value, int32_t font, uint8_t bold, uint8_t lines, int16_t width) -> int32_t {
            auto& self = *static_cast<NetworkModeSelectionActivity*>(context);
            self.textTarget.setFont(0, font);
            fui::TextStyle style;
            style.font = 0;
            style.bold = bold;
            style.maxLines = lines;
            return fui::measureWrappedText(self.textTarget, value, style, width).height;
          },
          [](void* context, int32_t font) -> int32_t {
            return static_cast<NetworkModeSelectionActivity*>(context)->renderer.getLineHeight(font);
          },
          MENU_ITEM_COUNT,
          0};
}
bool NetworkModeSelectionActivity::buildRustScene(XpUiScene& scene) {
  XpUiListState snapshot{};
  {
    ModelLock lock(*this);
    snapshot = state;
    renderingScroll = state.pending_scroll;
  }
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = textTarget.deviceContext();
  const auto safe = device.safeRect();
  XpUiSettingsLayout layout{};
  layout.list.screen = {0, 0, device.width, device.height};
  layout.list.area = {safe.x, safe.y, safe.width, safe.height};
  layout.list.font = fonts.bodyFontId;
  layout.list.row_height = mappedInput.hasTouch() ? 0 : m.listRowHeight;
  layout.list.row_gap = m.listRowGap;
  layout.list.inset = m.listInset;
  layout.list.padding = m.listSidePadding;
  layout.list.scroll_width = m.listScrollWidth;
  layout.list.radius = m.listRowRadius;
  layout.list.selection = m.listSelectionStyle;
  layout.list.scroll_left = m.listScrollSide;
  layout.list.bold = m.listTitleBold;
  layout.small_font = fonts.smallFontId;
  layout.top_padding = m.topPadding;
  layout.header_height = m.headerHeight;
  layout.hints_height = m.buttonHintsHeight;
  layout.spacing = m.verticalSpacing;
  layout.touch = mappedInput.hasTouch();
  const auto services = resources();
  const XpUiBuffer buffer{commands, nullptr, 24, 0};
  const bool built = xp_ui_network_modes_view(&snapshot, &layout, &services, &buffer, &next, &scene, &nextHits) == 1;
  return built;
}
void NetworkModeSelectionActivity::rustPublished() {
  xp_ui_settings_list_publish(&state, &next, renderingScroll);
  hits = nextHits;
  sceneFailed = false;
}
XpUiPaint NetworkModeSelectionActivity::rustResources() {
  return {this, nullptr, nullptr,
          [](void* context, uint32_t handle, uint32_t generation) -> uint8_t {
            if (generation != 1) return 0;
            if (handle == XP_UI_NETWORK_HEADER || handle == XP_UI_NETWORK_FOOTER) return 1;
            if (handle > 0 && (handle - 1) / 4 < MENU_ITEM_COUNT && (handle - 1) % 4 == 3) return 1;
            return static_cast<NetworkModeSelectionActivity*>(context)->text(handle) != nullptr;
          },
          [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
            auto& self = *static_cast<NetworkModeSelectionActivity*>(context);
            const auto r = command->rect;
            if (command->resource == XP_UI_NETWORK_HEADER) {
              GUI.drawHeader(self.renderer, Rect{r.x, r.y, r.w, r.h}, tr(STR_FILE_TRANSFER));
            } else if (command->resource == XP_UI_NETWORK_FOOTER) {
              const auto labels =
                  self.mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
              GUI.drawButtonHints(self.renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
            } else if (command->kind == XP_UI_BITMAP) {
              const auto old = self.renderer.getClipRect();
              self.renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
              self.textTarget.bitmap({r.x, r.y, r.w, r.h}, listIconFor(menuIcons[(command->resource - 1) / 4], 32),
                                     fui::BitmapMode::Contain,
                                     fui::Paint::solid(command->paint == 1 ? fui::Color::White : fui::Color::Black));
              self.renderer.setClipRect(old[0], old[1], old[2], old[3]);
            } else
              self.renderer.drawUiText(*command, clip, self.text(command->resource));
          }};
}
