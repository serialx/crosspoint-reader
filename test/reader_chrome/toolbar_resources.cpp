#include <xp_ui_reader_chrome.h>

#include <cassert>
#include <cstdlib>
#include <new>
static bool countAllocations = false;
static unsigned allocations = 0;
void* operator new(size_t size) {
  if (countAllocations) ++allocations;
  auto* result = std::malloc(size);
  if (!result) throw std::bad_alloc();
  return result;
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
struct EpdFontFamily {
  enum Style { Regular };
};
struct Metrics {
  int getTextWidth(int font, const char* text, EpdFontFamily::Style) {
    return std::strlen(text) * (font == 0 ? 8 : 10);
  }
  int getLineHeight(int font) { return font == 0 ? 18 : 24; }
};
struct ResourceTarget : Recorder {
  int mappedFont = 0;
  void setFont(int, int font) { mappedFont = font; }
  int16_t lineHeight(ui::FontId) const override { return mappedFont == 0 ? 18 : 24; }
  ui::Size measureText(ui::FontId, const char* text, ui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * (mappedFont == 0 ? 8 : 10)), lineHeight(0)};
  }
};
struct ReaderToolbarUi {
  Toolbar::Model model_;
  Metrics renderer;
  ResourceTarget textTarget;
  static constexpr int kMaxWindow = 16;
  std::string windowLabels_[16], windowValues_[16];
  struct {
    bool toggle, checked;
  } rowFlags[16]{};
  uint16_t windowFirst = 0, windowCount = 0;
  char pageIndicator[16]{};
  const char* text(uint32_t) const;
  XpUiReaderRows resources();
};
// RESOURCE_METHODS
int main() {
  for (int width : {528, 792}) {
    const int height = width == 528 ? 792 : 528;
    Toolbar old;
    ReaderToolbarUi now;
    int oldCalls = 0, newCalls = 0;
    old.model_.panel = now.model_.panel = true;
    old.model_.denseRows = now.model_.denseRows = true;
    old.model_.selectedIndex = now.model_.selectedIndex = 0;
    old.model_.rowText = [&](int) {
      ++oldCalls;
      return std::string(1024, 'A');
    };
    now.model_.rowText = [&](int) {
      ++newCalls;
      return std::string(1024, 'A');
    };
    XpUiReaderTheme theme{};
    theme.screen = theme.safe = {0, 0, static_cast<int16_t>(width), static_cast<int16_t>(height)};
    theme.body_font = 1;
    theme.small_font = 0;
    theme.title_font = 2;
    theme.row_min = 48;
    theme.row_padding = 8;
    theme.scroll_width = 3;
    theme.space_sm = 4;
    theme.space_md = 8;
    theme.space_lg = 16;
    theme.control_radius = theme.sheet_radius = 12;
    XpUiReaderToolbarModel model{};
    model.panel = 1;
    model.dense_rows = 1;
    model.panel_title = XP_UI_READER_HEADER;
    XpUiListState state{};
    state.selected = 0;
    state.follow = 1;
    XpUiCommand commands[128]{};
    XpUiBuffer buffer{};
    buffer.commands = commands;
    buffer.command_capacity = 128;
    for (int repaint = 0; repaint < 2; ++repaint) {
      Recorder target;
      fui::DeviceContext device;
      device.width = width;
      device.height = height;
      fui::InteractionBuffer<32> hits;
      fui::Frame<32> frame(target, device, {}, hits);
      auto tokens = fui::themeTokensForLineHeight(24);
      tokens.controlRadius = tokens.sheetRadius = 12;
      fui::Screen<32> screen(frame, tokens);
      oldCalls = 0;
      allocations = 0;
      countAllocations = true;
      old.buildPanel(screen);
      countAllocations = false;
      const unsigned oldAllocations = allocations;
      const auto rows = now.resources();
      XpUiListState next{};
      XpUiScene scene{};
      XpUiReaderHitLayout layout{};
      newCalls = 0;
      allocations = 0;
      countAllocations = true;
      const auto status = xp_ui_reader_toolbar_view(&state, &model, &theme, &rows, &buffer, &next, &scene, &layout);
      countAllocations = false;
      assert(status == XP_UI_READER_VIEW_OK);
      assert(oldCalls == old.nav_.visibleRows && newCalls == oldCalls);
      assert(allocations == oldAllocations && allocations == static_cast<unsigned>(newCalls));
      assert(next.visible == old.nav_.visibleRows && next.top == old.nav_.top);
      size_t oldPayload = 0, newPayload = 0;
      for (int i = 0; i < 16; ++i) {
        oldPayload += old.windowLabels_[i].size();
        newPayload += now.windowLabels_[i].size();
      }
      assert(oldPayload == newPayload && newPayload == 1024u * newCalls);
      std::printf("resources %dx%d repaint %d: old/new callbacks %d/%d allocations %u/%u label bytes %zu/%zu\n", width,
                  height, repaint, oldCalls, newCalls, oldAllocations, allocations, oldPayload, newPayload);
      state = next;
    }
  }
}
