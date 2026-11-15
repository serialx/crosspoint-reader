// Frozen from 31d5cd46 CatalogActivity and OpdsBookBrowserActivity.
bool CatalogActivity::handleCustomInput() {
  if (state == State::WIFI_SELECTION || state == State::SEARCH_INPUT || state == State::DOWNLOADING) return true;
  if (state == State::ERROR) {
    // Back first: a header back tap is also a screen tap, which means Retry here.
    int x = 0, y = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onBackButton();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      if (wifiConnected())
        retryBrowse();
      else
        launchWifiSelection();
    }
    return true;
  }
  if (state == State::CHECK_WIFI || state == State::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onBackButton();
    return true;
  }
  // Previous on a searchable list's top row opens search on release. Lists
  // move on press, so latch that press instead of stepping; holding still
  // pages (navigateButtons() drops the latch when the selection moves).
  if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    searchPending = state == State::BROWSING && hasSearch() && nav.selected == 0;
    return searchPending;
  }
  if (searchPending && mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    searchPending = false;
    launchSearch();
    return true;
  }
  return false;
}

bool OpdsBookBrowserActivity::handleCustomInput() {
  if (state == State::BROWSING &&
      (!pageNextHref.empty() || !pagePrevHref.empty() || !pageFirstHref.empty() || !pageLastHref.empty())) {
    // On a paginated feed the side buttons belong to feed pages exclusively.
    // NavNext/NavPrevious resolve to the side buttons too, so every side
    // press/hold pass is swallowed here or the shared list nav would also
    // step the selection under the same physical button.
    if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      if (!pageNextHref.empty()) followPageLink(pageNextHref);
      return true;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      if (!pagePrevHref.empty()) followPageLink(pagePrevHref);
      return true;
    }
    if (mappedInput.isPressed(MappedInputManager::Button::PageForward) ||
        mappedInput.isPressed(MappedInputManager::Button::PageBack)) {
      return true;
    }
  }
  return CatalogActivity::handleCustomInput();
}
