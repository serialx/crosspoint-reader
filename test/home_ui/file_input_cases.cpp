int main() {
  using B = MappedInputManager::Button;
  for (int picker : {0, 1})
    for (const char* path : {"/", "/Books", "/Books/Child"}) {
      for (int selection : {-1, 0, 1, 2, 3})
        for (int event = 0; event < 13; ++event) {
          FileBrowserActivity f;
          f.mode = picker ? FileBrowserActivity::Mode::PickFirmware : FileBrowserActivity::Mode::Books;
          f.basepath = path;
          f.loadFiles();
          f.nav.reset(selection);
          f.nav.visibleRows = 2;
          f.nav.drawnRows = 2;
          ButtonNavigator::setMappedInputManager(f.mappedInput);
          auto& in = f.mappedInput;
          clockMs = 5000;
          if (event == 0) in.released = 1u << unsigned(B::Confirm);
          if (event == 1) in.longConfirm = true;
          if (event == 2) in.released = 1u << unsigned(B::Back);
          if (event == 3) {
            in.released = 1u << unsigned(B::Back);
            in.duration = 999;
          }
          if (event == 4) {
            in.released = 1u << unsigned(B::Back);
            in.duration = 1000;
          }
          if (event == 5) {
            in.released = 1u << unsigned(B::Back);
            in.duration = 1001;
          }
          if (event == 6) in.pressed = 1u << unsigned(B::NavNext);
          if (event == 7) in.pressed = 1u << unsigned(B::NavPrevious);
          if (event == 8) {
            in.held = 1u << unsigned(B::NavNext);
            in.duration = 501;
          }
          if (event == 9) {
            in.held = 1u << unsigned(B::NavPrevious);
            in.duration = 501;
          }
          if (event == 10) in.released = (1u << unsigned(B::Confirm)) | (1u << unsigned(B::Back));
          if (event == 11) {
            in.released = 1u << unsigned(B::Back);
            in.longConfirm = true;
          }
          const bool consumed = f.handleCustomInput() || f.handleButtons();
          if (!consumed) f.navigateButtons();
          std::printf("file %d %s %d %d: %d %d %d %d %d %s %s\n", picker, path, selection, event, int(f.nav.selected),
                      f.nav.top, int(f.nav.followOnBuild), f.updates, consumed, f.basepath.c_str(), f.effect.c_str());
        }
    }
}
