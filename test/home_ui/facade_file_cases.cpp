int main() {
  XpUiFileFacts empty{};
  XpUiFileState picker{};
  xp_ui_file_init(&picker, 1);
  assert(xp_ui_file_activate(nullptr, &empty).kind == 0);
  assert(xp_ui_file_activate(&picker, nullptr).kind == 0);
  assert(xp_ui_file_activate(&picker, &empty).kind == 0);
  empty.count = 1;
  assert(xp_ui_file_activate(&picker, &empty).kind == XP_UI_FILE_PICK);
  picker.list.selected = -1;
  assert(xp_ui_file_activate(&picker, &empty).kind == 0);
  for (bool directory : {false, true}) {
    for (bool touch : {false, true}) {
      for (bool reveal : {false, true}) {
        FileBrowserActivity browser;
        browser.mappedInput.touch = touch;
        browser.state.list.selected = directory ? 2 : 1;
        browser.state.list.top = 1;
        if (touch && reveal) browser.state.revealed = browser.state.list.selected;
        browser.mappedInput.longPress = true;
        browser.loop();
        assert(browser.optionPopup.isActive());
        browser.mappedInput.longPress = false;
        // The input owner suppresses the release following a physical long press.
        browser.loop();
        browser.mappedInput.pressed = XP_UI_CONFIRM;
        browser.loop();
        browser.mappedInput.pressed = 0;
        browser.mappedInput.released = XP_UI_CONFIRM;
        browser.loop();
        assert(!browser.optionPopup.isActive());
        assert(browser.effects.size() == 2);
        assert(browser.effects.back().kind == (directory ? XP_UI_FILE_DIRECTORY : XP_UI_FILE_OPEN));
        assert(browser.effects.back().value == (directory ? 2 : 1));
        // Directory resources are still old until the effect handler loads them.
        assert(browser.state.list.selected == (directory ? 2 : 1));
        assert(browser.state.list.top == 1);
      }
    }
  }
  FileBrowserActivity cancel;
  cancel.mappedInput.longPress = true;
  cancel.loop();
  cancel.mappedInput.longPress = false;
  cancel.mappedInput.released = XP_UI_BACK;
  cancel.loop();
  assert(!cancel.optionPopup.isActive() && cancel.effects.size() == 1);
  // Touch-opened menu has the same production callback and ignores retained facts.
  FileBrowserActivity touch;
  touch.mappedInput.touch = touch.mappedInput.longTouch = true;
  touch.publishedHits.first = {0, 0, 50, 50};
  touch.publishedHits.first_index = 1;
  touch.publishedHits.heights[0] = 50;
  touch.publishedHits.count = 1;
  touch.loop();
  assert(touch.optionPopup.isActive());
  touch.mappedInput.longTouch = false;
  touch.mappedInput.released = XP_UI_CONFIRM;
  touch.loop();
  assert(touch.effects.size() == 2 && touch.effects[1].kind == XP_UI_FILE_OPEN);
}
