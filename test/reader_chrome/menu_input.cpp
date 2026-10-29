#include <cstdio>

// Frozen menu action branches and popup selections from 386e3c45.
int main() {
  for (int orientation = 0; orientation < 4; ++orientation) {
    for (int pageTurn = 0; pageTurn < 5; ++pageTurn) {
      for (int action = 0; action < 19; ++action) {
        int effect = 6;
        if (action == 8)
          effect = 1;
        else if (action == 7)
          effect = 2;
        else if (action == 3)
          effect = 3;
        else if (action == 5)
          effect = 4;
        else if (action == 4)
          effect = 5;
        std::printf("activate %d %d %d: %d %d %d %d 0\n", orientation, pageTurn, action, effect, action, orientation,
                    pageTurn);
      }
      std::printf("cancel %d %d: 6 -1 %d %d 1\n", orientation, pageTurn, orientation, pageTurn);
      for (int selection = 0; selection < 4; ++selection)
        std::printf("orientation %d %d %d: 7 %d %d\n", orientation, pageTurn, selection, selection, pageTurn);
      for (int selection = 0; selection < 5; ++selection)
        std::printf("page-turn %d %d %d: 8 %d %d\n", orientation, pageTurn, selection, orientation, selection);
    }
  }
}
