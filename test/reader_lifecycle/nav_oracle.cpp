#include <cstdio>
#include <initializer_list>

#include "components/lists/list.h"
#if READER_NAV_RUST
#include <xp_ui_reader.h>
#endif
int main() {
  for (int count : {0, 1, 2, 7, 19, 100})
    for (int top : {0, 1, 5, 21})
      for (int visible : {0, 1, 5, 12})
        for (int drawn : {0, 1, 3, 9})
          for (int drawnCount : {0, 7, 19, 100})
            for (int delta : {-20, -1, 0, 1, 20}) {
#if READER_NAV_RUST
              XpUiListState n{};
              xp_ui_reader_toolbar_nav_reset(&n);
              n.top = top;
              n.visible = visible;
              n.drawn = drawn;
              n.drawn_count = drawnCount;
              const int rows = xp_ui_reader_toolbar_page_rows(&n);
              const bool moved = xp_ui_reader_toolbar_scroll(&n, delta, count);
#else
              freeink::ui::ListNav n;
              n.reset();
              n.top = top;
              n.visibleRows = visible;
              n.drawnRows = drawn;
              n.drawnCount = drawnCount;
              const int rows = n.pageRows();
              const bool moved = n.scrollBy(delta, count);
#endif
              std::printf("nav %d %d %d %d %d %d %d %d %d\n", count, top, visible, drawn, drawnCount, delta, rows,
                          moved, n.top);
            }
}
