// Frozen from 074bac4a: original custom dispatch and popup touch owner.
bool Original::handleCustomInput() {
  if (ignoreHubInstallerBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ignoreHubInstallerBackRelease = false;
      return true;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) ignoreHubInstallerBackRelease = false;
  }
  if (pluginHubPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;

  if (state == State::AUTH) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::NO_TOKEN;
      requestUpdate();
      return true;
    }
    if (static_cast<long>(millis() - authNextPollMs) >= 0) pollAuth();
    return true;
  }

  if (state == State::NO_TOKEN) {
    // Back first: a header back tap is also a screen tap, which means sign in here.
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      exitCatalog();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(tx, ty)) {
      if (!wifiConnected())
        launchWifiSelection();
      else
        retryBrowse();
    }
    return true;
  }

  if (state == State::DONE) {
    int tx = 0;
    int ty = 0;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(tx, ty)) {
      // The just-finished download may have installed/updated a plugin; refresh
      // the install badges so the row no longer reads "Update".
      computeInstallStatus();
      releaseRows();
      state = State::BROWSING;
      requestUpdate();
    }
    return true;
  }

  return CatalogActivity::handleCustomInput();
}
  bool Popup::handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const freeink::ui::InputSnapshot snap = touchSnapshotFrom(input);
    if (snap.touchPressed || snap.touchReleased || snap.touchHeld) {
      // Interactions are registered on the render task; only route once the
      // first render after show() has populated the table (uiReady handshake).
      if (uiReady) {
        const freeink::ui::ActionEvent event = interactions.routePublished(snap);
        if (event && event.action == ACTION_OPTION) {
          // Tap released on an option: select it, fire, dismiss.
          selectedIndex = event.value;
          active = false;
          if (onSelectCallback) onSelectCallback(selectedIndex);
          haptic_feedback::touchAction();
          requestUpdate();
          return true;
        }
        if (event && event.action == ACTION_CHROME) {
          // Taps on the dialog chrome (title, padding) keep the popup open.
          return true;
        }
        if (snap.touchReleased && snap.touchX >= 0) {
          // Tap released outside the dialog: dismiss without firing. Swipe-end
          // releases arrive with -1,-1 coords and fall through (no dismiss).
          active = false;
          haptic_feedback::touchAction();
          requestUpdate();
          return true;
        }
        if (snap.touchPressed) {
          // Touch-down on an option moves the highlight (route() latched the
          // hit as the active interaction; read it back, no re-hit-testing).
          const int16_t idx = interactions.activeIndex();
          if (idx >= 0) {
            const freeink::ui::Interaction& hit = interactions.publishedData()[idx];
            if (hit.action == ACTION_OPTION && selectedIndex != hit.value) {
              selectedIndex = hit.value;
              requestUpdate();
            }
          }
        }
      }
      return true;
    }

    return handleButtons(input.wasPressed(MappedInputManager::Button::NavPrevious),
                         input.wasPressed(MappedInputManager::Button::NavNext),
                         input.wasReleased(MappedInputManager::Button::Confirm),
                         input.wasReleased(MappedInputManager::Button::Back), requestUpdate);
  }
