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
  void bitmap(ui::Rect r, ui::BitmapRef bitmap, ui::BitmapMode mode, ui::Paint p, ui::Rotation rotation) override {
    rect("bitmap", r);
    std::printf(" %u %u %u %u %u\n", bitmap.width, bitmap.height, unsigned(mode), unsigned(p.color),
                unsigned(rotation));
  }
};

// SDK placements used by reader lists and toolbar sheets at 386e3c45.
int main() {
  static const uint8_t iconBytes[128]{};
  const ui::BitmapRef icon{iconBytes, 32, 32};
  for (const int width : {480, 528, 792, 800}) {
    for (const int kind : {0, 1, 2, 3}) {
      for (const int selected : {0, 3, 18}) {
        for (const auto selection : {ui::SelectionStyle::InvertFill, ui::SelectionStyle::LightPill,
                                     ui::SelectionStyle::Underline, ui::SelectionStyle::Triangle}) {
          std::printf("case %d %d %d %u\n", width, kind, selected, unsigned(selection));
          Recorder target;
          ui::DeviceContext device;
          device.width = width;
          device.height = 480;
          ui::InteractionBuffer<32> hits;
          ui::Frame<32> frame(target, device, {}, hits);
          auto theme = ui::themeTokensForLineHeight(24);
          theme.listSelectionStyle = selection;
          theme.listRowRadius = 12;
          theme.listRowGap = 3;
          theme.listInset = 5;
          ui::Screen<32> screen(frame, theme);
          ui::ListItem rows[19];
          for (int i = 0; i < 19; ++i) {
            rows[i].label = i % 3 == 1 ? "A long chapter title that wraps across the row beside a value" : "Chapter";
            rows[i].actionValue = i;
            if (kind == 1) {
              rows[i].value = i % 2 ? "Portrait" : nullptr;
              rows[i].toggle = i % 2 == 0;
              rows[i].toggleChecked = i % 4 == 0;
            }
            if (kind == 2) {
              rows[i].subtitle = "28% - 5/12 - A chapter title";
              rows[i].icon = icon;
            }
            if (kind == 3) {
              rows[i].subtitle = "A clipping preview with enough words to wrap to a second line on a narrow display";
              rows[i].icon = icon;
            }
          }
          ui::ListNav nav;
          nav.reset(selected);
          ui::ListProps props;
          props.items = rows;
          props.count = 19;
          props.action = 1;
          props.inputMask = ui::InputTouch | (kind >= 2 ? ui::InputLongPress : 0);
          props.partialTrailingRow = true;
          props.toggleCheckbox = true;
          props.toggleWidth = 28;
          props.toggleHeight = 28;
          if (kind == 1) {
            props.labelText = theme.smallText;
            props.labelText.maxLines = 2;
            props.valueInset = 8;
          }
          screen.syncListViewport(nav, props, 19);
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
  for (const int width : {480, 528, 792, 800}) {
    for (const int radius : {0, 12, 36}) {
      Recorder target;
      ui::DeviceContext device;
      device.width = width;
      device.height = width < 600 ? 792 : 480;
      ui::InteractionBuffer<32> hits;
      ui::Frame<32> frame(target, device, {}, hits);
      auto theme = ui::themeTokensForLineHeight(24);
      theme.controlRadius = radius;
      theme.sheetRadius = radius;
      ui::Screen<32> screen(frame, theme);
      ui::SheetProps sheet;
      sheet.anchor = ui::SheetEdge::Bottom;
      sheet.dismissAction = 1;
      sheet.grabberMargin = theme.spaceLg;
      sheet.grabberInset = theme.spaceLg + theme.spaceMd;
      std::printf("sheet %d %d\n", width, radius);
      screen.sheet(sheet, 240);
      screen.insetContent(ui::Insets{0, theme.spaceLg, 0, theme.spaceLg});
      ui::ButtonProps step;
      step.label = nullptr;
      step.icon = icon;
      step.action = 3;
      step.inputMask = ui::InputTouch;
      step.styles.explicitlySet = true;
      step.styles.normal.background = ui::Paint::solid(ui::Color::White);
      step.styles.normal.foreground = ui::Paint::solid(ui::Color::Black);
      step.styles.normal.border = ui::Paint::solid(ui::Color::Black);
      step.styles.normal.borderWidth = 1;
      step.styles.normal.radius = radius;
      screen.spacer(theme.spaceMd);
      const auto band = screen.takeTop(36, theme.spaceLg);
      screen.button(step, ui::Rect{band.x, band.y, 36, 36});
      step.action = 4;
      screen.button(step, ui::Rect{static_cast<int16_t>(band.right() - 36), band.y, 36, 36});
      for (size_t i = 0; i < hits.count(); ++i) {
        const auto& hit = hits.data()[i];
        Recorder::rect("hit", hit.rect);
        std::printf(" %u %d\n", hit.action, hit.value);
      }
    }
  }
  return 0;
}
