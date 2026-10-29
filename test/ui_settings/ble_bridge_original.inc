// Input methods frozen from bcdfb338 (original C++ settings).
void BleSettingsActivity::loop() {
  // Holding Confirm on an action clears it (touch: a long press on the row, onRowLongPress).
  if (view == View::Mapping && mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, CLEAR_HOLD_MS)) {
    clearMapping(nav.selected);
  }
  UiListActivity::loop();
}
void UiListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value;
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}
bool UiListActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (selected >= 0 && selected < listCount()) activateIndex(selected);
    return true;
  }
  return false;
}
