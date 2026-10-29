#include "Recorder.h"

int main() {
  Recorder target;
  const char* subtitles[] = {"", "Short", "A subtitle long enough to wrap to the second line"};
  for (int width : {120, 240})
    for (int padding : {-1, 0, 8})
      for (int minimum : {36, 70})
        for (int lines : {1, 2})
          for (bool balance : {false, true})
            for (int text = 0; text < 3; ++text) {
              ui::ListItem item;
              item.label = "A long network or plugin name";
              item.subtitle = subtitles[text];
              item.value = "||||";
              ui::ListProps props;
              props.rowHeight = minimum;
              props.rowPaddingY = padding;
              props.sidePadding = 8;
              props.textGap = 10;
              props.valueInset = 8;
              props.labelText.font = 1;
              props.labelText.maxLines = 2;
              props.subtitleText.font = 0;
              props.subtitleText.maxLines = lines;
              props.valueText.font = 0;
              props.balanceWrappedLabelWithValue = balance;
              const auto m = ui::measureListRow(target, nullptr, width, props, item);
              std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", width, padding, minimum, lines, balance, text,
                          m.labelWidth, m.labelHeight, m.subtitleHeight, m.valueWidth, m.height);
            }
}
