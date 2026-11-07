#include "NetworkModeSelectionActivity.h"

#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/OwnedLegacyActivity.h"
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
    : Activity("NetworkModeSelection", renderer, input), textTarget(makeUiTarget(renderer)) {
  for (int i = 0; i < MENU_ITEM_COUNT; ++i)
    rows[i] = {I18N.get(menuItems[i]), I18N.get(menuDescs[i]), nullptr, static_cast<uint32_t>(i + 1), 0, 0, 1, 0};
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
void NetworkModeSelectionActivity::physicalLayout(XpUiSettingsLayout& layout) {
  const auto& m = UITheme::getInstance().getMetrics();
  const auto fonts = uiScaleSpec();
  const auto device = textTarget.deviceContext();
  const auto safe = device.safeRect();
  layout = {};
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
}

void NetworkModeSelectionActivity::paint(const XpUiCommand& command, XpUiRect clip) {
  const auto r = command.rect;
  if (command.resource == XP_UI_NETWORK_HEADER) {
    GUI.drawHeader(renderer, Rect{r.x, r.y, r.w, r.h}, tr(STR_FILE_TRANSFER));
  } else if (command.resource == XP_UI_NETWORK_FOOTER) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (command.kind == XP_UI_BITMAP) {
    const auto old = renderer.getClipRect();
    renderer.setClipRect(clip.x, clip.y, clip.w, clip.h);
    textTarget.bitmap({r.x, r.y, r.w, r.h}, listIconFor(menuIcons[(command.resource - 1) / 4], 32),
                      fui::BitmapMode::Contain,
                      fui::Paint::solid(command.paint == 1 ? fui::Color::White : fui::Color::Black));
    renderer.setClipRect(old[0], old[1], old[2], old[3]);
  } else
    renderer.drawUiText(command, clip, text(command.resource));
}

void NetworkModeSelectionActivity::prepareResult(bool cancelled, uint8_t mode) {
  if (cancelled) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
  } else {
    setResult(NetworkModeResult{static_cast<NetworkMode>(mode)});
  }
}

namespace {
NetworkModeSelectionActivity& modes(void* object) {
  return *static_cast<NetworkModeSelectionActivity*>(nativeActivity(object));
}
}  // namespace
const XpNetworkModesPhysical XP_NETWORK_MODES_PHYSICAL = {
    [](void* object, XpUiSettingsLayout* out) { modes(object).physicalLayout(*out); },
    [](void* object) { return modes(object).resources(); },
    [](void* object, const XpUiCommand* command, XpUiRect clip) { modes(object).paint(*command, clip); },
    [](int32_t delta, uint16_t count) {
      LOG_DBG("LIST", "%s swipe delta=%d count=%d", "NetworkModeSelection", static_cast<int>(delta), count);
    },
    [](uint8_t invalid) {
      if (invalid)
        LOG_ERR("RUI", "Invalid scene in %s", "NetworkModeSelection");
      else
        LOG_ERR("RUI", "Cannot build scene in %s", "NetworkModeSelection");
    },
    [](void* object, uint8_t cancelled, uint8_t mode) { modes(object).prepareResult(cancelled != 0, mode); }};

#if defined(CONFIG_IDF_TARGET_ESP32C3)
static_assert(sizeof(NetworkModeSelectionActivity) == 208);
#endif
