int main() {
  GfxRenderer renderer;
  BaseTheme base;
  LyraTheme lyra;
  Lyra3CoversTheme three;
  RoundedRaffTheme rounded;
  for (int width : {480, 528, 792, 800})
    for (int theme = 0; theme < 4; ++theme)
      for (int books : {0, 1, 3})
        for (int selected : {0, 1, 3, 5})
          for (int status = 0; status < 3; ++status)
            for (int snapshot : {0, 1, 2}) {
              screenWidth = width;
              coverStatus = status;
              bitmapWidth = 113;
              coverWidth = 113;
              std::printf("view %d %d %d %d %d %d\n", width, theme, books, selected, status, snapshot);
              std::vector<RecentBook> recents;
              for (int i = 0; i < books; ++i)
                recents.push_back({i == 1 ? "Short" : "A book title with enough words to occupy multiple lines",
                                   i == 1 ? "" : "An author", status == 0 ? "" : std::to_string(i)});
              const Rect cover{6, 60, width - 12, 250};
              bool rendered = snapshot == 2, stored = rendered, restored = rendered;
              auto save = [&]() {
                if (snapshot) primitive(5, cover.x, cover.y, cover.width, cover.height, 0, 0, 0, 0x80000004u);
                return snapshot != 0;
              };
              primitive(9, 0, 0, width, 800, 1);
              if (restored) primitive(5, cover.x, cover.y, cover.width, cover.height, 0, 0, 0, 0x80000003u);
              primitive(5, 0, 10, width, 50, 0, 0, 0, 0x80000001u);
              switch (theme) {
                case 0:
                  base.drawRecentBookCover(renderer, cover, recents, selected, rendered, stored, restored, save);
                  break;
                case 1:
                  lyra.drawRecentBookCover(renderer, cover, recents, selected, rendered, stored, restored, save);
                  break;
                case 2:
                  three.drawRecentBookCover(renderer, cover, recents, selected, rendered, stored, restored, save);
                  break;
                case 3:
                  rounded.drawRecentBookCover(renderer, cover, recents, selected, rendered, stored, restored, save);
                  break;
              }
              const Rect menu{6, 322, width - 12, 628};
              const auto label = [](int i) { return std::string("Menu ") + std::to_string(i); };
              const auto icon = [](int i) { return static_cast<UIIcon>(i); };
              const int menuCount = 5 + (theme == 3 && books > 0),
                        menuSelection = theme == 3 ? selected : selected - books;
              switch (theme) {
                case 0:
                  base.drawButtonMenu(renderer, menu, menuCount, menuSelection, label, icon);
                  break;
                case 1:
                case 2:
                  lyra.drawButtonMenu(renderer, menu, menuCount, menuSelection, label, icon);
                  break;
                case 3:
                  rounded.drawButtonMenu(renderer, menu, menuCount, menuSelection, label, icon);
                  break;
              }
              primitive(5, 0, 0, width, 800, 0, 0, 0, 0x80000002u);
            }
}
