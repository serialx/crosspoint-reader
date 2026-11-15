int main() {
  using B = MappedInputManager::Button;
  for (int tab = 0; tab < 3; ++tab)
    for (int count : {0, 1, 12})
      for (int initial : {0, 1, 12})
        for (int flags = 0; flags < 8; ++flags)
          for (int event = 0; event < 15; ++event) {
            LibraryListActivity l;
            l.activeTabIndex = tab;
            l.sortOrder = orderForTab(tab, l.descendingTabs);
            l.count = count;
            l.degraded = flags & 1;
            l.groupsCollapsed = flags & 2;
            l.query = flags & 4 ? "query" : "";
            l.activeNav().reset(initial);
            l.activeNav().visibleRows = 3;
            l.activeNav().drawnRows = 2;
            l.navigationStartedOnTabs = initial == 0;
            ButtonNavigator::setMappedInputManager(l.mappedInput);
            clockMs = 5000;
            auto& in = l.mappedInput;
            switch (event) {
              case 0:
                in.released = 1u << unsigned(B::Confirm);
                break;
              case 1:
                in.longConfirm = true;
                break;
              case 2:
                in.released = 1u << unsigned(B::Back);
                break;
              case 3:
                in.pressed = 1u << unsigned(B::NavNext);
                break;
              case 4:
                in.pressed = 1u << unsigned(B::NavPrevious);
                break;
              case 5:
                in.released = 1u << unsigned(B::NavPrevious);
                break;
              case 6:
                in.held = 1u << unsigned(B::NavNext);
                in.duration = 501;
                break;
              case 7:
                in.held = 1u << unsigned(B::NavPrevious);
                in.duration = 501;
                break;
              case 8:
                in.released = (1u << unsigned(B::Confirm)) | (1u << unsigned(B::Back));
                break;
              case 9:
                in.released = 1u << unsigned(B::Back);
                in.longConfirm = true;
                break;
              case 10:
                in.released = 1u << unsigned(B::Confirm);
                l.lockNextConfirmRelease = true;
                break;
              case 11:
                in.released = (1u << unsigned(B::Confirm)) | (1u << unsigned(B::Back));
                l.lockNextConfirmRelease = true;
                l.lockNextBackRelease = true;
                break;
              case 12:
                in.released = 1u << unsigned(B::Back);
                l.lockNextBackRelease = true;
                break;
              case 13:
                in.pressed = (1u << unsigned(B::NavNext)) | (1u << unsigned(B::NavPrevious));
                break;
            }
            const bool consumed = l.handleCustomInput() || l.handleButtons();
            if (!consumed) l.navigateButtons();
            std::printf("library %d %d %d %d %d: %d %d %d %d %d %d %d %d %d %d %d %d %s\n", tab, count, initial, flags,
                        event, l.activeTabIndex, int(l.descendingTabs), int(l.sortOrder), l.ringPos(),
                        l.activeNav().top, int(l.activeNav().followOnBuild), l.groupsCollapsed, !l.query.empty(),
                        l.lockNextConfirmRelease, l.lockNextBackRelease, l.updates, consumed, l.effect.c_str());
          }
}
