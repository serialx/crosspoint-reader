int main() {
  using B = MappedInputManager::Button;
  for (int grid : {0, 1}) for (int count : {0, 1, 3, 7}) for (int slot : {0, 1, 2}) {
    for (int initial = 0; initial < count + 4 + (slot != 0); ++initial) {
      for (int event = 0; event < 25; ++event) {
        HomeActivity h;
        h.hasPlugins = slot == 2;
        h.hasOpdsServers = slot == 1;
        h.hasContinueReading = count != 0;
        for (int i = 0; i < count; ++i) h.recentBooks.push_back({std::to_string(i)});
        if (grid) h.coverGridUi = std::make_unique<CoverGridHomeUi>();
        h.selectorIndex = initial;
        ButtonNavigator::setMappedInputManager(h.mappedInput);
        haptics = 0;
        clockMs = 5000;
        auto& in = h.mappedInput;
        const B buttons[] = {B::NavNext, B::NavPrevious, B::Up, B::Down, B::Left, B::Right};
        if (event < 6) in.pressed = 1u << unsigned(buttons[event]);
        else if (event < 12) { in.held = 1u << unsigned(buttons[event - 6]); in.duration = 501; }
        else if (event == 12) in.released = 1u << unsigned(B::Back);
        else if (event == 13) in.released = 1u << unsigned(B::Confirm);
        else if (event == 14) in.swipe = MappedInputManager::SwipeDir::Up;
        else if (event == 15) in.swipe = MappedInputManager::SwipeDir::Down;
        else if (event == 16) { in.touch=1; in.x=30; in.y=90; }
        else if (event == 17) { in.touch=2; in.x=30; in.y=90; }
        else if (event == 18) { in.touch=2; in.x=30; in.y=332; }
        else if (event == 19) { in.touch=2; in.x=30; in.y=368; }
        else if (event == 20) { in.held=1u << unsigned(B::NavNext); in.duration=500; }
        else if (event == 21) { in.pressed=(1u << unsigned(B::NavNext)) | (1u << unsigned(B::NavPrevious)); }
        else if (event == 22) { in.pressed=1u << unsigned(B::NavNext); in.swipe=MappedInputManager::SwipeDir::Up; }
        else if (event == 23) { in.released=(1u << unsigned(B::Confirm)) | (1u << unsigned(B::Back)); }
        h.loop();
        std::printf("home %d %d %d %d %d: %d %d %d %d\n", grid, count, slot, initial, event,
                    h.selectorIndex, h.effect, h.updates, haptics);
        if (event >= 6 && event < 12) {
          for (int ms : {500, 501, 1001, 1002}) {
            clockMs = 5000 + ms;
            h.loop();
            std::printf("repeat %d: %d %d\n", ms, h.selectorIndex, h.updates);
          }
        }
      }
    }
  }
}
