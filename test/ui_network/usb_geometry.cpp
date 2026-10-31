#include <FreeInkApp.h>

#include <cassert>
#include <cstdint>
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
  enum class State : uint8_t { Unsupported, WaitingForHost, Connected, Ejected, Disconnected, IoError };

  bool preparing = true, startFailed = false;
  State state = State::Unsupported;
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
      case State::WaitingForHost:
        message = tr(STR_USB_DRIVE_WAITING);
        break;
      case State::Connected:
        message = tr(STR_USB_DRIVE_CONNECTED);
        detail = tr(STR_USB_DRIVE_CONNECT_DELAY);
        secondaryDetail = tr(STR_USB_DRIVE_EJECT_HINT);
        break;
      case State::IoError:
        message = startFailed ? tr(STR_USB_DRIVE_START_ERROR) : tr(STR_USB_DRIVE_ERROR);
        break;
      case State::Ejected:
      case State::Disconnected:
      case State::Unsupported:
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
int main() {
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
              UsbDriveActivity a;
              a.state = static_cast<UsbDriveActivity::State>(phase);
              a.preparing = preparing;
              a.startFailed = failed;
              a.buildDriveScreen(screen);
            }
}
