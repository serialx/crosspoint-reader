#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <fontIds.h>
#include <xp_ui_network.h>

#include <cassert>
#include <cstdio>

#include "Trace.h"
void textGoldenGlyph(const EpdFontFamily&, uint32_t, int, int, EpdFontFamily::Style) {}
void textGoldenScaled(int, uint32_t, int, int, EpdFontFamily::Style) {}
namespace ui = freeink::ui;
namespace fui = freeink::ui;
enum TextId {
  STR_USB_DRIVE_PREPARING,
  STR_USB_DRIVE_EJECT_HINT,
  STR_USB_DRIVE_WAITING,
  STR_USB_DRIVE_CONNECTED,
  STR_USB_DRIVE_CONNECT_DELAY,
  STR_USB_DRIVE_START_ERROR,
  STR_USB_DRIVE_ERROR
};
static const char* const words[2][7] = {
    {"Preparing USB Drive", "Safely eject the drive before disconnecting", "Waiting for USB host",
     "USB Drive connected", "The drive may take a moment to appear", "Unable to start USB Drive", "USB Drive error"},
    {"A longer preparation message that must wrap across the panel width",
     "Eject the drive safely before disconnecting this cable and returning to the reader home screen",
     "Waiting for a connection to the USB host computer", "Connection established with the USB host computer",
     "The computer may take several moments to recognize the newly connected removable storage device",
     "The reader could not start the USB mass storage session", "The connected USB drive encountered an I/O error"}};
static unsigned language = 0;
#define tr(x) words[language][x]
struct ThemeMetrics {
  int topPadding = 12, headerHeight = 36, buttonHintsHeight = 40, contentSidePadding = 16;
} metrics;
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const ThemeMetrics& getMetrics() { return metrics; }
};
using UiScreen = ui::Screen<24>;
struct UsbDriveActivity {
  using State = XpUiUsbPhase;
  struct Phase {
    static constexpr auto WaitingForHost = XP_UI_USB_WAITING, Connected = XP_UI_USB_CONNECTED,
                          IoError = XP_UI_USB_IO_ERROR, Ejected = XP_UI_USB_EJECTED,
                          Disconnected = XP_UI_USB_DISCONNECTED, Unsupported = XP_UI_USB_UNSUPPORTED;
  };
  bool preparing = true, startFailed = false;
  XpUiUsbPhase state = XP_UI_USB_UNSUPPORTED;
  void buildDriveScreen(UiScreen&) const;
};
void UsbDriveActivity::buildDriveScreen(UiScreen& screen) const {
  const char* message = nullptr;
  const char* detail = nullptr;
  const char* secondaryDetail = nullptr;
  if (preparing) {
    message = tr(STR_USB_DRIVE_PREPARING);
    detail = tr(STR_USB_DRIVE_EJECT_HINT);
  } else {
    switch (state) {
      case Phase::WaitingForHost:
        message = tr(STR_USB_DRIVE_WAITING);
        break;
      case Phase::Connected:
        message = tr(STR_USB_DRIVE_CONNECTED);
        detail = tr(STR_USB_DRIVE_CONNECT_DELAY);
        secondaryDetail = tr(STR_USB_DRIVE_EJECT_HINT);
        break;
      case Phase::IoError:
        message = startFailed ? tr(STR_USB_DRIVE_START_ERROR) : tr(STR_USB_DRIVE_ERROR);
        break;
      case Phase::Ejected:
      case Phase::Disconnected:
      case Phase::Unsupported:
        return;
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), static_cast<int16_t>(metrics.contentSidePadding),
      static_cast<int16_t>(metrics.buttonHintsHeight), static_cast<int16_t>(metrics.contentSidePadding)});

  auto messageStyle = screen.theme().smallText;
  messageStyle.align = fui::TextAlign::Center;
  messageStyle.bold = true;
  messageStyle.maxLines = 2;
  auto detailStyle = screen.theme().smallText;
  detailStyle.align = fui::TextAlign::Center;
  detailStyle.maxLines = 3;

  const fui::Rect body = screen.body();
  const int16_t messageHeight = fui::measureWrappedText(screen.target(), message, messageStyle, body.width).height;
  const int16_t detailHeight =
      detail ? fui::measureWrappedText(screen.target(), detail, detailStyle, body.width).height : 0;
  const int16_t secondaryDetailHeight =
      secondaryDetail ? fui::measureWrappedText(screen.target(), secondaryDetail, detailStyle, body.width).height : 0;
  const int16_t gap = detail ? screen.theme().spaceMd : 0;
  const int16_t secondaryGap = secondaryDetail ? screen.theme().spaceMd : 0;
  const int16_t totalHeight =
      static_cast<int16_t>(messageHeight + gap + detailHeight + secondaryGap + secondaryDetailHeight);
  if (body.height > totalHeight) screen.spacer(static_cast<int16_t>((body.height - totalHeight) / 2));

  screen.target().text(screen.takeTop(messageHeight, gap), message, messageStyle);
  if (detail) {
    screen.target().text(screen.takeTop(detailHeight, secondaryGap), detail, detailStyle);
  }
  if (secondaryDetail) {
    screen.target().text(screen.takeTop(secondaryDetailHeight), secondaryDetail, detailStyle);
  }
}
static const char* label(uint32_t handle) {
  switch (handle) {
    case 1100:
      return tr(STR_USB_DRIVE_PREPARING);
    case 1101:
      return tr(STR_USB_DRIVE_EJECT_HINT);
    case 1102:
      return tr(STR_USB_DRIVE_WAITING);
    case 1103:
      return tr(STR_USB_DRIVE_CONNECTED);
    case 1104:
      return tr(STR_USB_DRIVE_CONNECT_DELAY);
    case 1105:
      return tr(STR_USB_DRIVE_START_ERROR);
    case 1106:
      return tr(STR_USB_DRIVE_ERROR);
    default:
      return "";
  }
}

int main() {
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont small(&ubuntu_10_regular), bold(&ubuntu_10_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &bold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  unsigned cases = 0;
  for (language = 0; language < 2; ++language)
    for (int orientation = 0; orientation < 4; ++orientation)
      for (int gap : {0, 12, 20})
        for (int inset : {0, 30})
          for (int phase = 0; phase < 6; ++phase)
            for (bool preparing : {false, true})
              for (bool failed : {false, true}) {
                renderer.setOrientation(static_cast<GfxRenderer::Orientation>(orientation));
                renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
                ui::GfxRendererTarget target(renderer, false);
                target.setFont(0, UI_10_FONT_ID);
                auto device = target.deviceContext();
                device.safeArea = {static_cast<int16_t>(inset), static_cast<int16_t>(inset),
                                   static_cast<int16_t>(inset), static_cast<int16_t>(inset)};
                auto theme = ui::themeTokensForLineHeight(renderer.getLineHeight(UI_10_FONT_ID));
                theme.spaceMd = gap;
                ui::InteractionBuffer<24> hits;
                ui::Frame<24> frame(target, device, {}, hits);
                UiScreen screen(frame, theme);
                renderer.clearScreen();
                UsbDriveActivity original;
                original.state = static_cast<XpUiUsbPhase>(phase);
                original.preparing = preparing;
                original.startFailed = failed;
                original.buildDriveScreen(screen);
                const auto expected = display.pixels;
                XpUiUsbState state{};
                state.phase = phase;
                state.preparing = preparing;
                state.start_failed = failed;
                const auto safe = device.safeRect();
                XpUiNetworkLayout layout{{0, 0, device.width, device.height},
                                         {safe.x, safe.y, safe.width, safe.height},
                                         UI_10_FONT_ID,
                                         12,
                                         36,
                                         40,
                                         16,
                                         static_cast<int16_t>(gap),
                                         0};
                XpUiNetworkTextServices texts{
                    &target,
                    [](void* context, uint32_t h, int32_t, int16_t width, uint8_t bold, uint8_t lines) -> int16_t {
                      ui::TextStyle style;
                      style.font = 0;
                      style.align = ui::TextAlign::Center;
                      style.bold = bold;
                      style.maxLines = lines;
                      return ui::measureWrappedText(*static_cast<ui::GfxRendererTarget*>(context), label(h), style,
                                                    width)
                          .height;
                    }};
                XpUiCommand commands[6]{};
                XpUiBuffer buffer{commands, nullptr, 6, 0};
                XpUiScene scene{};
                assert(xp_ui_network_usb_view(&state, &layout, &texts, &buffer, &scene));
                XpUiPaint paint{
                    &renderer, nullptr, nullptr, [](void*, uint32_t, uint32_t g) -> uint8_t { return g == 1; },
                    [](void* context, const XpGlyphTarget*, const XpUiCommand* command, XpUiRect clip) {
                      if (command->kind == XP_UI_TEXT_HANDLE)
                        static_cast<GfxRenderer*>(context)->drawUiText(*command, clip, label(command->resource));
                    }};
                assert(renderer.drawUiScene(scene, &paint));
                if (display.pixels != expected) {
                  fprintf(
                      stderr,
                      "USB pixels differ: language=%u orientation=%d gap=%d inset=%d phase=%d preparing=%d failed=%d\n",
                      language, orientation, gap, inset, phase, preparing, failed);
                  return 1;
                }
                ++cases;
              }
  printf("%u USB body pixel cases passed\n", cases);
}
