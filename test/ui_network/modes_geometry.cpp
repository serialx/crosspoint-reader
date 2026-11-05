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
int main() {
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
              activity.buildScreen(screen);
              printf("nav %d %d %d %d\n", activity.nav.selected.load(), activity.nav.top, activity.nav.drawnRows,
                     activity.nav.inputPageRows());
              for (size_t i = 0; i < hits.count(); ++i) {
                const auto& hit = hits.data()[i];
                Recorder::rect("hit", hit.rect);
                printf(" %u %d\n", hit.action, hit.value);
              }
            }
}
