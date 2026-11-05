// Physical drawing target for the shipping toolbar composition.
#include <FreeInkApp.h>
#include <FreeInkUIIcon.h>
#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <array>
#include <map>
#include <string_view>

namespace BookTextUi {
namespace fui = freeink::ui;
struct EpdFontFamily {
  enum Style { Regular };
};
static std::map<uint32_t, std::string> drawn;
static std::vector<std::string> measured;
class GfxRenderer {
 public:
  int getTextWidth(int, const char* value, EpdFontFamily::Style) const {
    measured.emplace_back(value);
    return int(std::strlen(value));
  }
  int getLineHeight(int) const { return 18; }
  void drawUiText(const XpUiCommand& command, XpUiRect, const char* value) {
    drawn[command.resource] = value ? value : "";
  }
  bool drawUiScene(const XpUiScene& scene, const XpUiPaint* paint) {
    for (uint16_t i = 0; i < scene.command_count; ++i) {
      const auto& command = scene.commands[i];
      if (command.resource && paint->resource_valid(paint->context, command.resource, command.generation))
        paint->resource(paint->context, nullptr, &command, command.rect);
    }
    return true;
  }
  std::array<int, 4> getClipRect() const { return {0, 0, 528, 792}; }
  void setClipRect(int, int, int, int) {}
};
class TextTarget : public fui::DrawTarget {
  fui::DeviceContext device;

 public:
  TextTarget() {
    device.width = 528;
    device.height = 792;
  }
  const fui::DeviceContext& deviceContext() const { return device; }
  void setFont(int, int) {}
  fui::Rect clipRect() const override { return {0, 0, 528, 792}; }
  bool setClipRect(fui::Rect) override { return true; }
  fui::Size measureText(fui::FontId, const char* value, fui::TextStyle) const override {
    measured.emplace_back(value);
    return {int16_t(std::strlen(value)), 18};
  }
  int16_t lineHeight(fui::FontId) const override { return 18; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {}, fui::Rotation = fui::Rotation{}) override {}
};
TextTarget makeUiTarget(GfxRenderer&) { return {}; }
struct UITheme {
  struct Metrics {
    int listRowHeight = 30;
  };
  static UITheme& getInstance() {
    static UITheme value;
    return value;
  }
  Metrics getMetrics() const { return {}; }
};
void sampleReaderChromeTheme(const GfxRenderer&, XpUiReaderTheme& theme) {
  theme.screen = theme.safe = {0, 0, 528, 792};
  theme.body_font = theme.title_font = theme.small_font = 0;
  theme.row_min = 30;
  theme.row_padding = 8;
  theme.scroll_width = 3;
  theme.space_sm = 4;
  theme.space_md = 8;
  theme.space_lg = 16;
  theme.control_radius = theme.sheet_radius = 12;
}
class MappedInputManager;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
static const freeink::Icon icon_reader_contents_24{}, icon_reader_text_24{}, icon_reader_more_24{},
    icon_reader_back_24{}, icon_reader_next_24{};
// ACTUAL_TOOLBAR
// ACTUAL_TOOLBAR_LEAF
static GfxRenderer renderer;
static ReaderToolbarUi toolbar(renderer);
static void checkToolbar(XpAppText title, const std::string& expected) {
  assert(title.len == expected.size());
  const auto prefix = expected.substr(0, expected.find('\0'));
  for (uint8_t panel : {0, 1}) {
    drawn.clear();
    measured.clear();
    XpEpubToolbarModel model{};
    model.chapter = model.page_info = model.panel_title = title;
    model.selected = -1;
    model.panel = panel;
    assert(paint(&toolbar, &model, nullptr, nullptr));
    if (panel)
      assert(drawn.at(XP_UI_READER_HEADER) == prefix);
    else {
      assert(drawn.at(XP_UI_READER_TITLE) == prefix);
      assert(drawn.at(XP_UI_READER_PROGRESS) == prefix);
    }
  }
  // Row sinks are length-aware, including exact-sized sources with no terminator.
  std::vector<uint8_t> source(expected.begin(), expected.end());
  XpAppText span{source.data(), source.size()};
  XpEpubToolbarModel model{};
  model.panel = 1;
  model.panel_title = title;
  model.item_count = 1;
  model.selected = 0;
  drawn.clear();
  assert(paint(
      &toolbar, &model,
      [](void* raw, int32_t index, XpEpubToolbarRowSink sink, void* context) -> uint8_t {
        assert(index == 0);
        const auto value = *static_cast<XpAppText*>(raw);
        XpEpubToolbarRow row{value, value, 0, 0, 0};
        return sink(context, &row);
      },
      &span));
  assert(drawn.at(1) == prefix);
  if (!expected.empty()) assert(drawn.at(2) == prefix);
}
}  // namespace BookTextUi
