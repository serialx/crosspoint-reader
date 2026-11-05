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
  // OptionPopup Back/outside cancellation never invokes its selection callback.
  for (int option = 0; option < 2; ++option) std::printf("popup-cancel %d: 0 -1 3 4 1\n", option);
  // buildMenuItems remains the parent-owned resource adapter. Only exposed
  // rows can dispatch a capability-dependent service action.
  const int actions[] = {0, 1, 9, 12, 10, 11, 3, 4, 5, 18, 8, 7, 6, 13, 14, 15, 16, 17, 2};
  for (int caps = 0; caps < 8; ++caps) {
    std::printf("capabilities %d:", caps);
    for (int action : actions) {
      if (action == 4 && (!(caps & 1) || (caps & 2))) continue;
      if (action == 5 && !(caps & 4)) continue;
      const int effect = action == 8 ? 1 : action == 7 ? 2 : action == 3 ? 3 : action == 5 ? 4 : action == 4 ? 5 : 6;
      std::printf(" %d=%d", action, effect);
    }
    std::printf("\n");
  }
}
