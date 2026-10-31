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

static void hits(const ui::InteractionBuffer<24>& table) {
  for (size_t i = 0; i < table.count(); ++i) {
    const auto& h = table.data()[i];
    Recorder::rect("hit", h.rect);
    std::printf(" %u %d %u\n", h.action, h.value, unsigned(h.inputMask));
  }
}
int main() {
  for (const int width : {480, 528, 792, 800}) {
    for (const int radius : {0, 12, 18}) {
      Recorder target;
      ui::DeviceContext device;
      device.width = width;
      device.height = 792;
      ui::InteractionBuffer<24> table;
      ui::Frame<24> frame(target, device, {}, table);
      for (const int selected : {0, 1}) {
        std::printf("confirmation %d %d %d\n", width, radius, selected);
        table.clear();
        ui::DialogOption options[2];
        options[0].label = "Cancel";
        options[1].label = "Confirm";
        for (int i = 0; i < 2; ++i) {
          options[i].action = 1;
          options[i].value = i;
          options[i].state = i == selected ? ui::StateFocused : ui::StateNormal;
        }
        ui::OptionDialogProps p;
        p.title = "Remove from Recent Books?";
        p.headline = "A long title with more than one line of text in the narrow dialog";
        p.titleText.font = 1;
        p.titleText.bold = true;
        p.titleText.align = ui::TextAlign::Center;
        p.titleText.maxLines = 2;
        p.headlineText.font = 1;
        p.headlineText.align = ui::TextAlign::Center;
        p.headlineText.maxLines = 3;
        p.buttonText.font = 1;
        p.options = options;
        p.optionCount = 2;
        p.verticalOptions = true;
        p.inputMask = ui::InputTouch;
        p.padding = {16, 16, 16, 16};
        p.gap = 8;
        p.buttonHeight = 40;
        p.styles = ui::defaultPopupStyles();
        p.styles.normal.border = ui::Paint::solid(ui::Color::Black);
        p.styles.normal.borderWidth = 2;
        p.styles.normal.radius = radius;
        if (radius) {
          p.buttonStyles = ui::defaultButtonStyles();
          p.buttonStyles.focused = p.buttonStyles.selected;
          ui::setStyleRadius(p.buttonStyles, radius);
        }
        const int16_t w = width * 3 / 4, h = ui::optionDialogHeight(target, p, w);
        const auto r = ui::centeredRect(device.screen(), {w, h});
        frame.hit(r, 2, 0, ui::InputTouch);
        ui::optionDialog(frame, r, p);
        hits(table);
      }
      for (const int value : {0, 1, 50, 99, 100}) {
        std::printf("capsule %d %d %d\n", width, radius, value);
        table.clear();
        ui::CapsuleSliderProps p;
        p.value = value;
        p.radius = radius;
        p.action = 1;
        ui::capsuleSlider(frame, {30, 100, static_cast<int16_t>(width - 60), 56}, p);
        hits(table);
      }
      for (const bool toggle : {false, true}) {
        std::printf("slider-row %d %d %d\n", width, radius, toggle);
        table.clear();
        ui::SliderRowProps p;
        p.label = "Brightness";
        p.value = "60%";
        p.sliderValue = 60;
        p.sliderAction = 1;
        p.decrement = 4;
        p.increment = 4;
        p.toggleAction = toggle ? 3 : 0;
        p.labelText.font = 0;
        p.valueText.font = 0;
        p.buttonText.font = 2;
        p.buttonRadius = radius;
        p.capsuleRadius = radius;
        ui::sliderRow(frame, {16, 40, static_cast<int16_t>(width - 32), 82}, p);
        hits(table);
      }
      std::printf("sheet %d %d\n", width, radius);
      table.clear();
      ui::SheetProps sheet;
      sheet.radius = radius;
      sheet.grabberMargin = 16;
      sheet.grabberInset = 24;
      ui::sheet(frame, {0, 0, static_cast<int16_t>(width), 300}, sheet);
      hits(table);
      for (const bool cover : {false, true}) {
        std::printf("details %d %d %d\n", width, radius, cover);
        table.clear();
        ui::PublicationPageProps p;
        p.book.title = "A publication title";
        p.book.author = "An author";
        p.book.series = "Source";
        p.book.titleText.font = 1;
        p.book.detailText.font = 1;
        p.book.seriesLines = 1;
        if (!cover) {
          p.book.coverSize = {0, 0};
          p.book.titleLines = 2;
          p.book.authorLines = 1;
          p.heroHeight = 4 * 24 + 20 + 6;
        }
        p.availability.status = "Available";
        p.availability.copies = "2 copies";
        p.availability.holds = "No holds";
        p.availability.headingText.font = 1;
        p.availability.detailText.font = 1;
        p.metadata = "Version 1";
        p.metadataLines = 3;
        p.descriptionHeading = "Description";
        p.description = "Some description text that wraps across lines.";
        p.headingText.font = 1;
        p.bodyText.font = 1;
        p.primary.label = "Read";
        p.primary.action = 1;
        p.more.label = "Details";
        p.more.action = 2;
        for (auto* b : {&p.primary, &p.more}) {
          b->text.font = 1;
          b->radius = radius;
          b->styles = ui::defaultButtonStyles();
          b->styles.normal.border = ui::Paint::solid(ui::Color::Black);
          b->styles.normal.borderWidth = 1;
        }
        p.actionHeight = 56;
        ui::publicationPage(frame, {0, 80, static_cast<int16_t>(width), 660}, p);
        hits(table);
      }
    }
  }
  // SDK contact grab/release semantics, including an off-target drag release.
  ui::InteractionBuffer<24> table;
  table.addInteraction({{10, 10, 100, 40}, 1, 7, ui::InputTouch | ui::InputDrag, ui::StateNormal});
  for (const auto s : {ui::InputSnapshot{}}) {
    (void)s;
  }
  for (int phase = 0; phase < 5; ++phase) {
    ui::InputSnapshot s;
    s.touchX = phase == 0 ? 20 : (phase < 3 ? 150 : -1);
    s.touchY = 20;
    s.touchHeld = phase < 3;
    s.touchPressed = phase == 0;
    s.touchReleased = phase == 3;
    const auto e = table.route(s);
    std::printf("route %d %u %d %d\n", phase, e.action, e.value, e.dragPermille);
  }
}
