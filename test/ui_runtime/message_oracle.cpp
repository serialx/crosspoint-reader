#include <algorithm>
#include <cstdio>
#include <cstring>

// FullScreenMessageActivity::onEnter at 1d95ed1d, with recorded font services.
static void message(int screenWidth, int screenHeight, int lineHeight, int textWidth, int style, int refresh) {
  const auto height = lineHeight;
  const auto top = (screenHeight - height) / 2;
  std::printf("clear\n");
  const int x = (screenWidth - textWidth) / 2;
  std::printf("text %d %d %d\nrefresh %d\n", x, top, style, refresh);
}
int main() {
  for (const int width : {480, 528, 792, 800}) {
    for (const int textWidth : {0, 73, 900}) {
      for (const int style : {0, 1, 2, 3}) {
        std::printf("case %d %d %d\n", width, textWidth, style);
        message(width, width == 528 ? 792 : 480, 23, textWidth, style, 1);
      }
    }
  }
}
