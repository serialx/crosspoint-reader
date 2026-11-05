#include <xp_ui_network.h>

#include <cassert>

#include "../ui_settings/Recorder.h"
namespace fui = freeink::ui;
using UiScreen = ui::Screen<24>;
struct ThemeMetrics {
  int topPadding = 12, headerHeight = 36, buttonHintsHeight = 40, verticalSpacing = 8;
} metrics;
struct UITheme {
  static UITheme& getInstance() {
    static UITheme t;
    return t;
  }
  const ThemeMetrics& getMetrics() { return metrics; }
};
struct NetworkModeSelectionActivity {
  int MENU_ITEM_COUNT;
  static constexpr auto ACTION_ROW = 1;
  ui::ListNav nav;
  ui::ListItem rowItems_[4]{};
  ui::ListNav& activeNav() { return nav; }
  int listCount() const { return MENU_ITEM_COUNT; }
  void buildScreen(UiScreen&);
  void syncListViewport(UiScreen&, ui::ListProps&, int = 0);
};
#include "modes_original.inc"
int main(int argc, char**) {
  const bool candidate = argc > 1;
  const char* labels[] = {"Join a Network", "Connect to Calibre", "Create Hotspot", "USB Drive"};
  const char* descriptions[] = {"Connect to an existing WiFi network with a deliberately long description that wraps",
                                "Transfer books from a Calibre library over the local wireless network",
                                "Create a wireless network", "Connect to a computer"};
  static const uint8_t icon[128] = {0xAA};
  for (int panel : {0, 1})
    for (int orientation = 0; orientation < 4; ++orientation)
      for (bool touch : {false, true})
        for (int count : {3, 4})
          for (int selected = 0; selected < count; ++selected)
            for (int selection = 0; selection < 4; ++selection) {
              int width = panel ? 528 : 480, height = panel ? 792 : 800;
              if (orientation & 1) std::swap(width, height);
              printf("modes %d %d %d %d %d %d\n", panel, orientation, touch, count, selected, selection);
              Recorder target;
              target.clip = {0, 0, static_cast<int16_t>(width), static_cast<int16_t>(height)};
              ui::DeviceContext device;
              device.width = width;
              device.height = height;
              device.hasTouch = touch;
              ui::InteractionBuffer<24> hits;
              ui::Frame<24> frame(target, device, {}, hits);
              auto theme = ui::themeTokensForLineHeight(24);
              theme.listSelectionStyle = static_cast<ui::SelectionStyle>(selection);
              theme.listRowGap = 3;
              theme.listInset = 5;
              theme.listRowRadius = 12;
              UiScreen screen(frame, theme);
              NetworkModeSelectionActivity activity{count};
              activity.nav.reset(selected);
              for (int i = 0; i < count; ++i) {
                auto& row = activity.rowItems_[i];
                row.label = labels[i];
                row.subtitle = descriptions[i];
                row.icon = {icon, 32, 32};
                row.actionValue = i;
              }
              if (!candidate) {
                activity.buildScreen(screen);
                printf("nav %d %d %d %d\n", activity.nav.selected.load(), activity.nav.top, activity.nav.drawnRows,
                       activity.nav.inputPageRows());
                for (size_t i = 0; i < hits.count(); ++i) {
                  const auto& hit = hits.data()[i];
                  Recorder::rect("hit", hit.rect);
                  printf(" %u %d\n", hit.action, hit.value);
                }
                continue;
              }
              struct Owner {
                Recorder* target;
                NetworkModeSelectionActivity* activity;
              } owner{&target, &activity};
              XpUiSettingsResources resources{
                  &owner,
                  [](void* context, uint16_t index, XpUiSettingsRow* out) -> uint8_t {
                    const auto& row = static_cast<Owner*>(context)->activity->rowItems_[index];
                    *out = {row.label, row.subtitle, row.value, 1, 0, 0, 1, 0};
                    return 1;
                  },
                  [](void* context, const char* text, int32_t font, uint8_t) -> int32_t {
                    return static_cast<Owner*>(context)->target->measureText(font, text, {}).width;
                  },
                  [](void* context, const char* text, int32_t font, uint8_t bold, uint8_t lines,
                     int16_t width) -> int32_t {
                    ui::TextStyle style;
                    style.font = font;
                    style.bold = bold;
                    style.maxLines = lines;
                    return ui::measureWrappedText(*static_cast<Owner*>(context)->target, text, style, width).height;
                  },
                  [](void* context, int32_t font) -> int32_t {
                    return static_cast<Owner*>(context)->target->lineHeight(font);
                  },
                  static_cast<uint16_t>(count),
                  0};
              XpUiSettingsLayout layout{};
              layout.list.screen = {0, 0, static_cast<int16_t>(width), static_cast<int16_t>(height)};
              layout.list.area = layout.list.screen;
              layout.list.font = 1;
              layout.small_font = 0;
              layout.list.row_height = theme.listMinRowHeight;
              layout.list.row_gap = theme.listRowGap;
              layout.list.inset = theme.listInset;
              layout.list.padding = theme.listSidePadding;
              layout.list.scroll_width = theme.listScrollWidth;
              layout.list.radius = theme.listRowRadius;
              layout.list.selection = selection;
              layout.list.scroll_left = theme.listScrollSide;
              layout.list.bold = theme.bodyText.bold;
              layout.top_padding = 12;
              layout.header_height = 36;
              layout.hints_height = 40;
              layout.spacing = 8;
              layout.touch = touch;
              XpUiListState state{};
              state.selected = selected;
              state.visible = 1;
              state.follow = 1;
              state.active = -1;
              XpUiListState next{};
              next.selected = 123;
              XpUiSettingsHits routing{};
              routing.count = 99;
              XpUiScene scene{};
              scene.generation = 123;
              XpUiCommand commands[24]{};
              XpUiBuffer buffer{commands, nullptr, 2, 0};
              assert(xp_ui_network_modes_view(&state, &layout, &resources, &buffer, &next, &scene, &routing) == 2);
              assert(next.selected == 123 && routing.count == 99 && scene.generation == 123);
              buffer.command_capacity = 24;
              assert(xp_ui_network_modes_view(&state, &layout, &resources, &buffer, &next, &scene, &routing) == 1);
              for (unsigned i = 0; i < scene.command_count; ++i) {
                const auto& c = commands[i];
                if (c.kind == XP_UI_FILL) {
                  const auto paint = c.paint == 2
                                         ? ui::Paint::dither(ui::Color::LightGray)
                                         : ui::Paint::solid(c.paint == 1 ? ui::Color::White : ui::Color::Black);
                  target.fill({c.rect.x, c.rect.y, c.rect.w, c.rect.h}, paint, c.radius, 15);
                } else if (c.kind == XP_UI_TEXT_HANDLE) {
                  const auto& row = activity.rowItems_[(c.resource - 1) / 4];
                  ui::TextStyle style;
                  style.font = c.font;
                  style.align = static_cast<ui::TextAlign>(c.align);
                  style.color = c.paint == 1 ? ui::Color::White : ui::Color::Black;
                  style.bold = c.style;
                  style.maxLines = c.max_lines;
                  target.text({c.rect.x, c.rect.y, c.rect.w, c.rect.h},
                              (c.resource - 1) % 4 == 0 ? row.label : row.subtitle, style);
                } else if (c.kind == XP_UI_TRIANGLE) {
                  target.triangle(
                      {c.rect.x, c.rect.y}, {c.rect.x, static_cast<int16_t>(c.rect.y + c.rect.h)},
                      {static_cast<int16_t>(c.rect.x + c.rect.w), static_cast<int16_t>(c.rect.y + c.rect.h / 2)},
                      ui::Paint::solid(ui::Color::Black));
                }
              }
              printf("nav %d %d %d %d\n", next.selected, next.top, next.drawn, next.drawn > 0 ? next.drawn : 1);
              int y = routing.first_row.y;
              for (int i = 0; i < routing.count; ++i) {
                Recorder::rect("hit",
                               {routing.first_row.x, static_cast<int16_t>(y), routing.first_row.w, routing.heights[i]});
                printf(" 1 %d\n", routing.first_index + i);
                y += routing.heights[i] + routing.gap;
              }
            }
}
