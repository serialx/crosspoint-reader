#include <xp_ui_reader_chrome.h>

#include <cassert>
#include <cstring>

int main() {
  XpUiReaderMenuState state{};
  xp_ui_reader_menu_init(&state, 3);
  assert(state.orientation == 3 && state.page_turn == 0);
  auto result = xp_ui_reader_menu_activate(&state, XP_UI_READER_MENU_BLUETOOTH);
  assert(result.effect == XP_UI_READER_MENU_TOGGLE_BLUETOOTH);
  result = xp_ui_reader_menu_choose(&state, XP_UI_READER_MENU_CHOOSE_AUTO_TURN, 4);
  assert(result.effect == XP_UI_READER_MENU_REDRAW && state.page_turn == 4);
  const auto before = state;
  result = xp_ui_reader_menu_choose(&state, XP_UI_READER_MENU_CHOOSE_ORIENTATION, 4);
  assert(result.effect == XP_UI_READER_MENU_NONE && result.cancelled);
  assert(std::memcmp(&state, &before, sizeof(state)) == 0);
  result = xp_ui_reader_menu_cancel(&state);
  assert(result.effect == XP_UI_READER_MENU_FINISH && result.cancelled && result.action == -1);
  assert(result.orientation == 3 && result.page_turn == 4);
  return 0;
}
