#pragma once

#include <xp_ui.h>

#include "components/UiAppHelpers.h"

// Read the current mapped frame without polling hardware or consuming Home gestures.
template <bool ExplicitHome, typename Mapped, typename Home>
inline XpUiInput rustInputSnapshotImpl(const Mapped& mappedInput, Home home) {
  XpUiInput input{};
  constexpr MappedInputManager::Button buttons[] = {
      MappedInputManager::Button::Back,        MappedInputManager::Button::Confirm,
      MappedInputManager::Button::NavNext,     MappedInputManager::Button::NavPrevious,
      MappedInputManager::Button::Left,        MappedInputManager::Button::Right,
      MappedInputManager::Button::Up,          MappedInputManager::Button::Down,
      MappedInputManager::Button::Power,       MappedInputManager::Button::PageBack,
      MappedInputManager::Button::PageForward, MappedInputManager::Button::ScreenLeft,
      MappedInputManager::Button::ScreenRight, MappedInputManager::Button::ScreenUp,
      MappedInputManager::Button::ScreenDown};
  static_assert(sizeof(buttons) / sizeof(buttons[0]) <= 16);
  static_assert((XP_UI_BACK | XP_UI_CONFIRM | XP_UI_NAV_NEXT | XP_UI_NAV_PREVIOUS | XP_UI_LEFT | XP_UI_RIGHT |
                 XP_UI_UP | XP_UI_DOWN | XP_UI_POWER | XP_UI_PAGE_BACK | XP_UI_PAGE_FORWARD | XP_UI_SCREEN_LEFT |
                 XP_UI_SCREEN_RIGHT | XP_UI_SCREEN_UP | XP_UI_SCREEN_DOWN) <= UINT16_MAX);
  for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    if constexpr (ExplicitHome) {
      if (mappedInput.wasPressed(buttons[i], home)) input.pressed |= 1u << i;
      if (mappedInput.wasReleased(buttons[i], home)) input.released |= 1u << i;
    } else {
      if (mappedInput.wasPressed(buttons[i])) input.pressed |= 1u << i;
      if (mappedInput.wasReleased(buttons[i])) input.released |= 1u << i;
    }
    if (mappedInput.isPressed(buttons[i])) input.held |= 1u << i;
  }
  input.now_ms = millis();
  if constexpr (ExplicitHome)
    input.held_ms = mappedInput.getHeldTime(home);
  else
    input.held_ms = mappedInput.getHeldTime();
  const auto touch = touchSnapshotFrom(mappedInput);
  input.touch_x = touch.touchX;
  input.touch_y = touch.touchY;
  input.touch_flags = (touch.touchPressed ? XP_UI_TOUCH_PRESSED : 0) |
                      (touch.touchReleased ? XP_UI_TOUCH_RELEASED : 0) | (touch.touchHeld ? XP_UI_TOUCH_HELD : 0);
  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      input.swipe = XP_UI_SWIPE_UP;
      break;
    case MappedInputManager::SwipeDir::Down:
      input.swipe = XP_UI_SWIPE_DOWN;
      break;
    case MappedInputManager::SwipeDir::Left:
      input.swipe = XP_UI_SWIPE_LEFT;
      break;
    case MappedInputManager::SwipeDir::Right:
      input.swipe = XP_UI_SWIPE_RIGHT;
      break;
    case MappedInputManager::SwipeDir::None:
      break;
  }
  return input;
}

inline XpUiInput rustInputSnapshot(const MappedInputManager& mappedInput) {
  return rustInputSnapshotImpl<false>(mappedInput, 1 /* HomeButtonAction::Ignore; unused by this specialization */);
}

// The explicit Home argument replaces the native compatibility lookup.
template <typename Home>
inline XpUiInput rustInputSnapshot(const MappedInputManager& mappedInput, Home home) {
  return rustInputSnapshotImpl<true>(mappedInput, home);
}
