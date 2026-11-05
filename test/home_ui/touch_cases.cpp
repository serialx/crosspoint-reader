int main() {
  for (int family = 0; family < 5; ++family)
    for (int count : {0, 1, 5})
      for (int open : {-1, 0, 3})
        for (int event = 0; event < 10; ++event)
          for (int row : {-1, 0, 4, 5}) {
            UiListActivity ui;
            ui.library = family >= 2;
            ui.picker = family == 1;
            ui.tab = family == 3 ? 1 : 0;
            ui.collapsed = family == 4;
            ui.count = count;
            ui.revealedIndex.store(open);
            ui.nav.selected = 1;
            ui.nav.visibleRows = 3;
            ui.nav.drawnRows = 2;
            ui.nav.drawnCount = count;
            ui.nav.onListRendered(0, 2, true);
            scrollDelta = 0;
            ui.app.action = event == 3 ? 2 : 1;
            ui.app.value = row;
            auto& in = ui.mappedInput;
            in.x = 10;
            in.y = 10;
            if (event == 1 || event == 3) in.touch = 2;
            if (event == 2) in.touch = 3;
            if (event == 4) in.swipe = MappedInputManager::SwipeDir::Left;
            if (event == 5) in.swipe = MappedInputManager::SwipeDir::Right;
            if (event == 6) in.swipe = MappedInputManager::SwipeDir::Up;
            if (event == 7) in.swipe = MappedInputManager::SwipeDir::Down;
            if (event == 8) in.released = 1u << unsigned(MappedInputManager::Button::Back);
            if (event == 9) in.released = 1u << unsigned(MappedInputManager::Button::Confirm);
            ui.loop();
            printf("%d %d %d %d %d : %d %d %d %d %d %d\n", family, count, open, event, row, ui.nav.selected.load(),
                   ui.revealedIndex.load(), scrollDelta, ui.effect, ui.value, ui.updates);
          }
}
