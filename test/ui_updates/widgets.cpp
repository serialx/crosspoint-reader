#include "../ui_settings/Recorder.h"

int main() {
  for (const int width : {480, 528, 792, 800}) {
    for (const int radius : {0, 12, 18}) {
      for (bool message : {false, true}) {
        for (int selected : {0, 1}) {
          Recorder target;
          ui::DeviceContext device;
          device.width = width;
          device.height = 792;
          ui::InteractionBuffer<4> hits;
          ui::Frame<4> frame(target, device, {}, hits);
          ui::DialogOption options[2];
          options[0].label = "Cancel";
          options[1].label = message ? "Clear" : "Update";
          for (int n = 0; n < 2; ++n) {
            options[n].action = 1;
            options[n].value = n;
            options[n].state = selected == n ? ui::StateFocused : ui::StateNormal;
          }
          ui::OptionDialogProps p;
          p.title = message ? "Clear reading cache" : "New update";
          p.message = message ? "Remove the reading cache while keeping your books and settings. This long synthetic "
                                "warning wraps across several lines on small displays."
                              : nullptr;
          p.messageText.font = 1;
          p.messageText.align = ui::TextAlign::Center;
          p.messageText.maxLines = 6;
          p.options = options;
          p.optionCount = 2;
          p.verticalOptions = true;
          p.inputMask = ui::InputTouch;
          p.titleText.font = 1;
          p.titleText.bold = true;
          p.titleText.align = ui::TextAlign::Center;
          p.titleText.maxLines = 2;
          p.buttonText.font = 1;
          p.padding = {16, 16, 16, 16};
          p.gap = 8;
          p.buttonHeight = 40;
          p.styles = ui::defaultPopupStyles();
          p.styles.normal.border = ui::Paint::solid(ui::Color::Black);
          p.styles.normal.borderWidth = 2;
          p.styles.normal.radius = radius;
          p.styles.focused = p.styles.normal;
          if (radius) {
            p.buttonStyles = ui::defaultButtonStyles();
            p.buttonStyles.focused = p.buttonStyles.selected;
            ui::setStyleRadius(p.buttonStyles, radius);
          }
          const int16_t dialogWidth = width * 3 / 4;
          const int16_t height = ui::optionDialogHeight(target, p, dialogWidth);
          const ui::Rect area{int16_t((width - dialogWidth) / 2), int16_t((792 - height) / 2), dialogWidth, height};
          std::printf("popup %d %d %d %d\n", width, radius, message, selected);
          frame.hit(area, 2, 0, ui::InputTouch);
          ui::optionDialog(frame, area, p);
          for (size_t n = 0; n < hits.count(); ++n) {
            const auto& h = hits.data()[n];
            Recorder::rect("hit", h.rect);
            std::printf(" %u %d\n", h.action, h.value);
          }
        }
      }
    }
  }
}
