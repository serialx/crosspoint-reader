#include <algorithm>
#include <initializer_list>
#include <numeric>

#include "../ui_settings/Recorder.h"
namespace fui = freeink::ui;
struct UiAppHost {
  using UiScreen = fui::Screen<32>;
};
struct CatalogLine {
  const char* text;
  bool bold = false;
};
struct UITheme {
  struct Metrics {
    int contentSidePadding = 20;
  };
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  Metrics getMetrics() const { return {}; }
};
enum { STR_DOWNLOADING, STR_CANCEL };
const char* tr(int id) { return id == STR_DOWNLOADING ? "Downloading" : "Cancel"; }
#include "status_original.inc"
int main() {
  for (int width : {480, 528, 792, 800}) {
    for (bool touch : {false, true}) {
      for (int state = 0; state < 9; ++state) {
        std::printf("catalog %d %d %d\n", width, touch, state);
        Recorder target;
        target.clip = {0, 0, static_cast<int16_t>(width), 360};
        fui::DeviceContext device;
        device.width = width;
        device.height = 360;
        device.hasTouch = touch;
        fui::InteractionBuffer<32> hits;
        fui::Frame<32> frame(target, device, {}, hits);
        auto theme = fui::themeTokensForLineHeight(24);
        theme.controlRadius = 6;
        fui::Screen<32> screen(frame, theme);
        screen.setContentMarginFromScreen({48, 0, 36, 0});
        switch (state) {
          case 0:
            catalogCenteredBlock(screen, {{"Error", false},
                                          {"The fixture server could not return its catalog. Please try again."},
                                          {"Tap to retry"}});
            break;
          case 1:
            catalogCenteredBlock(screen, {{"Error", true}, {"Connection failed"}});
            break;
          case 2:
            catalogDownloadScreen(screen, "A long synthetic book title that wraps onto a second line", 1572864, 4194304,
                                  3);
            break;
          case 3:
            catalogDownloadScreen(screen, "The Observatory", 1572864, 0, 3);
            break;
          case 4:
            catalogDownloadScreen(screen, "The Observatory", 1023, 0, fui::NO_ACTION);
            break;
          case 5:
            screen.centeredText("No entries", theme.bodyText);
            break;
          case 6: {
            fui::ListItem rows[14];
            for (int i = 0; i < 14; ++i) {
              rows[i].label = i % 3 == 0 ? "Browse all books in the observatory collection" : "The Observatory";
              rows[i].subtitle = i % 3 == 0 ? nullptr : "Synthetic Author";
              rows[i].value = i % 3 == 0 ? ">" : i % 3 == 1 ? "$3.00" : nullptr;
              rows[i].sectionHeading = i == 0 ? "Browse" : i == 1 ? "Publications" : nullptr;
              rows[i].actionValue = i;
            }
            fui::ListNav nav;
            nav.reset();
            fui::ListProps props;
            props.items = rows;
            props.count = 14;
            props.action = 1;
            props.inputMask = fui::InputTouch;
            props.partialTrailingRow = true;
            props.valueInset = 8;
            screen.syncListViewport(nav, props, 14);
            screen.list(props);
            std::printf("nav %d %d %d %d\n", nav.selected.load(), nav.top, nav.drawnRows, nav.inputPageRows());
            break;
          }
          case 7: {
            fui::HeaderProps header;
            header.title = "Fixture catalog";
            header.borderEdges = fui::EdgeBottom;
            header.titleText = theme.titleText;
            header.titleText.align = theme.headerTitleAlign;
            header.styles = theme.popup;
            header.styles.normal.border = fui::Paint::solid(fui::Color::Black);
            header.styles.normal.borderWidth = 1;
            header.sidePadding = theme.headerSidePadding;
            header.minTouchSize = theme.minTouchSize;
            fui::header(frame, {0, 0, static_cast<int16_t>(width), 48}, header);
            break;
          }
          case 8: {
            fui::TabItem tabs[2];
            tabs[0].label = "Previous";
            tabs[0].value = 0;
            tabs[0].enabled = false;
            tabs[1].label = "Next";
            tabs[1].value = 1;
            fui::TabBarProps props;
            props.tabs = tabs;
            props.count = 2;
            props.action = 5;
            props.inputMask = fui::InputTouch;
            fui::tabBar(frame, {20, 280, static_cast<int16_t>(width - 40), 40}, props);
            break;
          }
        }
        for (size_t i = 0; i < hits.count(); ++i) {
          const auto& hit = hits.data()[i];
          Recorder::rect("hit", hit.rect);
          std::printf(" %u %d\n", hit.action, hit.value);
        }
      }
    }
  }
}
