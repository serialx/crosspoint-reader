#include <FreeInkApp.h>
#include <xp_ui_network.h>

#include <cassert>
#include <cstdio>
#include <cstring>

namespace ui = freeink::ui;

// Fixed metrics make layout and glyph origins independent of installed fonts.
class Recorder final : public ui::DrawTarget {
 public:
  ui::Rect clip{0, 0, 528, 792};
  ui::Rect clipRect() const override { return clip; }
  bool setClipRect(ui::Rect next) override {
    clip = next;
    rect("clip", next);
    return true;
  }
  ui::Size measureText(ui::FontId font, const char* text, ui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text ? text : "") * (font == 0 ? 8 : 10)), lineHeight(font)};
  }
  int16_t lineHeight(ui::FontId font) const override { return font == 0 ? 18 : 24; }
  static void rect(const char* op, ui::Rect r) { std::printf("%s %d %d %d %d", op, r.x, r.y, r.width, r.height); }
  void fill(ui::Rect r, ui::Paint p, uint8_t radius, uint8_t corners) override {
    if (p.kind == ui::PaintKind::None) return;
    rect("fill", r);
    std::printf(" %u %u %u %u\n", unsigned(p.kind), unsigned(p.color), radius, corners);
  }
  void stroke(ui::Rect r, ui::Paint p, uint8_t width, uint8_t radius, uint8_t corners) override {
    rect("stroke", r);
    std::printf(" %u %u %u %u %u\n", unsigned(p.kind), unsigned(p.color), width, radius, corners);
  }
  void line(ui::Point a, ui::Point b, uint8_t width, ui::Paint p) override {
    std::printf("line %d %d %d %d %u %u\n", a.x, a.y, b.x, b.y, width, unsigned(p.color));
  }
  void triangle(ui::Point a, ui::Point b, ui::Point c, ui::Paint p) override {
    std::printf("triangle %d %d %d %d %d %d %u\n", a.x, a.y, b.x, b.y, c.x, c.y, unsigned(p.color));
  }
  void text(ui::Rect r, const char* text, ui::TextStyle s) override {
    rect("text", r);
    std::printf(" %u %u %u %u %u %u %s\n", unsigned(s.font), unsigned(s.align), unsigned(s.color), s.maxLines,
                unsigned(s.bold), unsigned(s.inverted), text ? text : "");
    // Freeze the common single-line glyph origin used by GfxRendererTarget.
    const int width = measureText(s.font, text, s).width;
    if (width <= r.width) {
      int x = r.x;
      if (s.align == ui::TextAlign::Center) x += (r.width - width) / 2;
      if (s.align == ui::TextAlign::Right) x += r.width - width;
      const int dy = (r.height - lineHeight(s.font)) / 2;
      std::printf("origin %d %d\n", x, r.y + (dy > 0 ? dy : 0));
    }
  }
  void bitmap(ui::Rect, ui::BitmapRef, ui::BitmapMode, ui::Paint, ui::Rotation) override {}
};

namespace fui = freeink::ui;
#define tr(x) #x
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
int main(int argc, char** argv) {
  const bool candidate = argc > 1 && strcmp(argv[1], "rust") == 0;
  if (candidate) {
    XpUiUsbState state{};
    state.phase = XP_UI_USB_CONNECTED;
    XpUiNetworkLayout layout{{0, 0, 528, 792}, {0, 0, 528, 792}, 0, 12, 36, 40, 16, 8, 0};
    XpUiNetworkTextServices text{nullptr,
                                 [](void*, uint32_t, int32_t, int16_t, uint8_t, uint8_t) -> int16_t { return 18; }};
    XpUiCommand commands[6]{};
    XpUiBuffer buffer{commands, nullptr, 2, 0};
    XpUiScene scene{};
    scene.generation = 1234;
    assert(!xp_ui_network_usb_view(&state, &layout, &text, &buffer, &scene));
    assert(scene.generation == 1234);
    buffer.command_capacity = 6;
    assert(xp_ui_network_usb_view(&state, &layout, &text, &buffer, &scene));
    assert(scene.command_count == 5);
    layout.side_padding = -1;
    scene.generation = 1234;
    assert(!xp_ui_network_usb_view(&state, &layout, &text, &buffer, &scene));
    assert(scene.generation == 1234);
  }
  for (int width : {480, 528, 792, 800})
    for (int pad : {0, 12, 20})
      for (int inset : {0, 30})
        for (int phase = 0; phase < 6; ++phase)
          for (bool preparing : {false, true})
            for (bool failed : {false, true}) {
              printf("usb %d %d %d %d %d %d\n", width, pad, inset, phase, preparing, failed);
              Recorder target;
              ui::DeviceContext device;
              device.width = width;
              device.height = 792;
              device.safeArea = {static_cast<int16_t>(inset), static_cast<int16_t>(inset), static_cast<int16_t>(inset),
                                 static_cast<int16_t>(inset)};
              ui::InteractionBuffer<24> hits;
              ui::Frame<24> frame(target, device, {}, hits);
              auto theme = ui::themeTokensForLineHeight(24);
              theme.spaceMd = pad;
              UiScreen screen(frame, theme);
              if (!candidate) {
                UsbDriveActivity a;
                a.state = static_cast<XpUiUsbPhase>(phase);
                a.preparing = preparing;
                a.startFailed = failed;
                a.buildDriveScreen(screen);
                continue;
              }
              XpUiUsbState state{};
              state.phase = phase;
              state.preparing = preparing;
              state.start_failed = failed;
              const auto safe = device.safeRect();
              XpUiNetworkLayout layout{{0, 0, static_cast<int16_t>(width), 792},
                                       {safe.x, safe.y, safe.width, safe.height},
                                       theme.smallText.font,
                                       12,
                                       36,
                                       40,
                                       16,
                                       static_cast<int16_t>(pad),
                                       0};
              XpUiNetworkTextServices text{
                  &target,
                  [](void* context, uint32_t h, int32_t font, int16_t w, uint8_t bold, uint8_t lines) -> int16_t {
                    auto& target = *static_cast<Recorder*>(context);
                    ui::TextStyle style;
                    style.font = font;
                    style.bold = bold;
                    style.maxLines = lines;
                    style.align = ui::TextAlign::Center;
                    return ui::measureWrappedText(target, label(h), style, w).height;
                  }};
              XpUiCommand commands[6]{};
              XpUiBuffer buffer{commands, nullptr, 6, 0};
              XpUiScene scene{};
              assert(xp_ui_network_usb_view(&state, &layout, &text, &buffer, &scene));
              for (unsigned i = 0; i < scene.command_count; ++i) {
                const auto& c = commands[i];
                if (c.kind != XP_UI_TEXT_HANDLE) continue;
                ui::TextStyle style;
                style.font = c.font;
                style.bold = c.style;
                style.maxLines = c.max_lines;
                style.align = static_cast<ui::TextAlign>(c.align);
                target.text({c.rect.x, c.rect.y, c.rect.w, c.rect.h}, label(c.resource), style);
              }
            }
}
