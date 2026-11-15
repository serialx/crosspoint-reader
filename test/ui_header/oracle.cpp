#include <FreeInkUI.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#ifndef HEADER_BASELINE
#include "components/UiHeader.h"
#endif

namespace ui = freeink::ui;

class Recorder final : public ui::DrawTarget {
 public:
  ui::Rect clip{0, 0, 528, 792};
  ui::Rect clipRect() const override { return clip; }
  bool setClipRect(ui::Rect next) override {
    clip = next;
    rect("clip", next);
    std::puts("");
    return true;
  }
  static void rect(const char* op, ui::Rect r) { std::printf("%s %d %d %d %d", op, r.x, r.y, r.width, r.height); }
  ui::Size measureText(ui::FontId font, const char* text, ui::TextStyle) const override {
    std::printf("measure %u %s\n", unsigned(font), text ? text : "");
    return {static_cast<int16_t>(std::strlen(text ? text : "") * 8), 24};
  }
  int16_t lineHeight(ui::FontId font) const override {
    std::printf("height %u\n", unsigned(font));
    return 24;
  }
  void fill(ui::Rect r, ui::Paint p, uint8_t radius, uint8_t corners) override {
    rect("fill", r);
    std::printf(" %u %u %u %u\n", unsigned(p.kind), unsigned(p.color), radius, corners);
  }
  void stroke(ui::Rect r, ui::Paint p, uint8_t width, uint8_t radius, uint8_t corners) override {
    rect("stroke", r);
    std::printf(" %u %u %u %u %u\n", unsigned(p.kind), unsigned(p.color), width, radius, corners);
  }
  void line(ui::Point a, ui::Point b, uint8_t width, ui::Paint p) override {
    std::printf("line %d %d %d %d %u %u %u\n", a.x, a.y, b.x, b.y, width, unsigned(p.kind), unsigned(p.color));
  }
  void triangle(ui::Point a, ui::Point b, ui::Point c, ui::Paint p) override {
    std::printf("triangle %d %d %d %d %d %d %u %u\n", a.x, a.y, b.x, b.y, c.x, c.y, unsigned(p.kind),
                unsigned(p.color));
  }
  void text(ui::Rect r, const char* text, ui::TextStyle s) override {
    rect("text", r);
    std::printf(" %u %u %u %u %u %u %s\n", unsigned(s.font), unsigned(s.align), unsigned(s.color), s.maxLines,
                unsigned(s.bold), unsigned(s.inverted), text ? text : "");
  }
  void bitmap(ui::Rect r, ui::BitmapRef b, ui::BitmapMode mode, ui::Paint p, ui::Rotation rotation) override {
    rect("bitmap", r);
    std::printf(" %d %d %u %u %u %u %u\n", b.width, b.height, b.data[0], unsigned(mode), unsigned(p.kind),
                unsigned(p.color), unsigned(rotation));
  }
};

int main() {
  static const uint8_t pixels[]{0x80};
  for (int width : {480, 528, 792, 800}) {
    for (bool touch : {false, true}) {
      for (int mask = 0; mask < 8; ++mask) {
        for (int status = 0; status < 4; ++status) {
          for (bool full : {false, true}) {
            std::printf("case %d %d %d %d %d\n", width, touch, mask, status, full);
            Recorder target;
            target.clip = {0, 0, static_cast<int16_t>(width), 792};
            ui::DeviceContext device;
            device.width = width;
            device.height = 792;
            device.hasTouch = touch;
            const ui::InputSnapshot input{};
            ui::InteractionBuffer<24> hits;
            ui::Frame<24> frame(target, device, input, hits);
            if (full) {
              for (int i = 0; i < 23; ++i) frame.hit({0, 0, 1, 1}, 100 + i);
            }
            ui::HeaderProps props;
            props.title = "Library";
            props.titleText.align = status & 1 ? ui::TextAlign::Center : ui::TextAlign::Left;
            props.sidePadding = 12;
            props.titleOffsetY = status ? 16 : 0;
            props.status.showBattery = status != 0;
            props.status.battery.label = "57%";
            props.status.batteryLeft = status & 1;
            props.status.stripHeight = status ? 24 : 0;
            props.status.clockText = status > 1 ? "12:34" : nullptr;
            props.status.clockCentered = status == 3;
            props.styles = ui::defaultPopupStyles();
            props.borderEdges = ui::EdgeBottom;
            props.trailingStyles = ui::plainStyles(ui::Paint::solid(ui::Color::Black));
            props.leadingSize = props.trailingSize = 32;
            props.leadingAction = mask & 1 ? 6 : ui::NO_ACTION;
            props.trailingAction = mask & 2 ? 4 : ui::NO_ACTION;
            props.trailingAdjacentAction = mask & 4 ? 5 : ui::NO_ACTION;
            props.leadingIcon = {pixels, 1, 1};
            props.trailingIcon = {pixels, 1, 1};
            props.trailingAdjacentIcon = {pixels, 1, 1};
            const ui::Rect band{0, 10, static_cast<int16_t>(width), static_cast<int16_t>(status ? 80 : 48)};
#ifdef HEADER_BASELINE
            ui::header(frame, band, props);
#else
            paintUiHeader(frame, band, props);
#endif
            std::printf("hits %zu overflow %d\n", hits.count(), hits.overflowed());
            for (size_t i = 0; i < hits.count(); ++i) {
              const auto& hit = hits.data()[i];
              Recorder::rect("hit", hit.rect);
              std::printf(" %u %d %u %u\n", hit.action, hit.value, hit.inputMask, unsigned(hit.state));
            }
          }
        }
      }
    }
  }
}
