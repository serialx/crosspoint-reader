// Appended to the frozen C++ toolbar builder by test_oracle.py.
int main() {
  for (int width : {480, 528, 792, 800})
    for (int dense : {0, 1})
      for (int count : {2, 30})
        for (int selected : {0, count - 1}) {
          Recorder target;
          fui::DeviceContext device;
          device.width = width;
          device.height = width < 600 ? 792 : 480;
          fui::InteractionBuffer<32> hits;
          fui::Frame<32> frame(target, device, {}, hits);
          auto theme = fui::themeTokensForLineHeight(24);
          theme.controlRadius = theme.sheetRadius = 12;
          fui::Screen<32> screen(frame, theme);
          Toolbar tool;
          tool.model_.panel = true;
          tool.model_.denseRows = dense;
          tool.model_.itemCount = count;
          tool.model_.selectedIndex = selected;
          int labels = 0, values = 0;
          tool.model_.rowText = [&](int) {
            ++labels;
            return std::string(1024, 'A');
          };
          tool.model_.rowValue = [&](int) {
            ++values;
            return std::string();
          };
          tool.buildPanel(screen);
          std::printf("window %d %d %d %d: %d %d %d %d\n", width, dense, count, selected, tool.nav_.top, labels, values,
                      tool.nav_.visibleRows);
        }
}
