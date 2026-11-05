#include "Recorder.h"

int main() {
  for (int width : {480, 528, 792, 800}) {
    for (bool touch : {false, true}) {
      for (int selection : {0, 2, 9}) {
        for (int lines : {1, 2}) {
          for (auto style : {ui::SelectionStyle::InvertFill, ui::SelectionStyle::LightPill,
                             ui::SelectionStyle::Underline, ui::SelectionStyle::Triangle}) {
            std::printf("settings %d %d %d %d %u\n", width, touch, selection, lines, unsigned(style));
            Recorder target;
            target.clip = {0, 0, static_cast<int16_t>(width), 240};
            ui::DeviceContext device;
            device.width = width;
            device.height = 240;
            device.hasTouch = touch;
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
              rows[i].label = i % 3 == 0 ? "A long setting name that wraps across the available space" : "Setting";
              rows[i].value = i % 3 == 0 ? "Selected" : nullptr;
              rows[i].toggle = i % 3 == 1;
              rows[i].toggleChecked = i % 2 == 0;
              rows[i].subtitle = i % 4 == 0 ? "Remote status" : nullptr;
              rows[i].enabled = i != 2;
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
            props.toggleCheckbox = true;
            props.toggleWidth = 28;
            props.toggleHeight = 28;
            props.valueInset = 8;
            props.labelText = theme.smallText;
            props.labelText.maxLines = lines;
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
  }
  return 0;
}
