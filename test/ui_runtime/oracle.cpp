#include <FreeInkApp.h>

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

int main() {
  for (const int width : {480, 528, 792, 800}) {
    for (const int selection : {0, 2, 11}) {
      for (const auto style : {ui::SelectionStyle::InvertFill, ui::SelectionStyle::LightPill,
                               ui::SelectionStyle::Underline, ui::SelectionStyle::Triangle}) {
        std::printf("case %d %d %u\n", width, selection, unsigned(style));
        Recorder target;
        ui::DeviceContext device;
        device.width = width;
        device.height = 240;
        ui::InteractionBuffer<24> hits;
        ui::Frame<24> frame(target, device, {}, hits);
        auto theme = ui::themeTokensForLineHeight(24);
        theme.listSelectionStyle = style;
        theme.listRowRadius = 12;
        theme.listRowGap = 3;
        theme.listInset = 5;
        ui::Screen<24> screen(frame, theme);
        ui::ListItem rows[12];
        for (int i = 0; i < 12; ++i) {
          rows[i].label = i == 1 ? "A long chapter title that wraps at the right edge of the list" : "Chapter";
          rows[i].actionValue = i;
        }
        ui::ListNav nav;
        nav.reset(selection);
        ui::ListProps props;
        props.items = rows;
        props.count = 12;
        props.action = 1;
        props.inputMask = ui::InputTouch;
        props.partialTrailingRow = true;
        screen.syncListViewport(nav, props, 12);
        screen.list(props);
        std::printf("nav %d %d %d %d\n", nav.selected.load(), nav.top, nav.drawnRows, nav.inputPageRows());
        for (size_t i = 0; i < hits.count(); ++i) {
          const auto& hit = hits.data()[i];
          Recorder::rect("hit", hit.rect);
          std::printf(" %u %d\n", hit.action, hit.value);
        }
      }
    }
  }
}
