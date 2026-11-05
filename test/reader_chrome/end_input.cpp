#include <cstdio>
#include <initializer_list>
// Frozen EndOfBookOptions key policy at 386e3c45, after routed touch.
int main() {
  for (int count : {2, 4})
    for (int selected = 0; selected < count; ++selected)
      for (int press : {0, 1})
        for (int button = 0; button < 4; ++button)
          for (int edge : {0, 1})
            for (int held : {0, 999, 1000, 1001}) {
              int action = 0, next = selected;
              if (button == 1 && edge == 1)
                action = 2;
              else if (button == 0 && edge == 1 && held < 1000)
                action = 1;
              else if (edge == (press ? 0 : 1) && button == 2) {
                next = (selected - 1 + count) % count;
                action = 3;
              } else if (edge == (press ? 0 : 1) && button == 3) {
                next = (selected + 1) % count;
                action = 3;
              }
              std::printf("%d %d %d %d %d %d: %d %d\n", count, selected, press, button, edge, held, action, next);
            }
}
