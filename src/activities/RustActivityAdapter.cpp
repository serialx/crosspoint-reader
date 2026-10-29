#include "RustActivityAdapter.h"

#include "components/UiAppHelpers.h"

XpUiMetrics RustActivityAdapter::rustMetrics() const {
  return {
      &renderer,
      [](void* context, int32_t font, const uint8_t* text, uint16_t length, uint8_t style) {
        const auto provider = static_cast<GfxRenderer*>(context)->textProvider();
        return xp_native_width(&provider, font, text, length, style, -1);
      },
      [](void* context, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(context)->getLineHeight(font); }};
}

bool RustActivityAdapter::paintRustScene(const XpUiScene& scene) {
  const auto resources = rustResources();
  if (!renderer.drawUiScene(scene, &resources)) {
    LOG_ERR("RUI", "Invalid scene in %s", name.c_str());
    ModelLock lock(*this);
    if (auto* publication = rustPublication()) publication->ready = false;
    return false;
  }
  {
    ModelLock lock(*this);
    rustPublished();
    if (auto* publication = rustPublication()) {
      publication->scene = scene;
      publication->scene.commands = nullptr;
      publication->scene.command_count = 0;
      publication->scene.text = nullptr;
      publication->scene.text_len = 0;
      publication->ready = true;
    }
  }
  switch (static_cast<XpUiRefresh>(scene.refresh)) {
    case XP_UI_NONE:
      break;
    case XP_UI_FAST:
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
    case XP_UI_HALF:
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      break;
    case XP_UI_FULL:
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
      break;
  }
  return true;
}

void RustActivityAdapter::render(RenderLock&&) {
  XpUiScene scene{};
  if (!buildRustScene(scene)) {
    {
      ModelLock lock(*this);
      if (auto* publication = rustPublication()) publication->ready = false;
    }
    LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
    return;
  }
  paintRustScene(scene);
}

void RustActivityAdapter::loop() {
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
  for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
    if (mappedInput.wasPressed(buttons[i])) input.pressed |= 1u << i;
    if (mappedInput.wasReleased(buttons[i])) input.released |= 1u << i;
    if (mappedInput.isPressed(buttons[i])) input.held |= 1u << i;
  }
  input.now_ms = millis();
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
  XpUiEffect effect{};
  {
    ModelLock lock(*this);
    const auto interest = rustInterest(input.now_ms);
    if (!xp_ui_input_due(&interest, &input)) return;
    const auto* publication = rustPublication();
    if (publication && publication->ready &&
        (input.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_RELEASED | XP_UI_TOUCH_HELD))) {
      XpUiInteraction hit{};
      if (xp_ui_hit(&publication->scene, input.touch_x, input.touch_y, 1, &hit)) {
        input.action = hit.action;
        input.value = hit.value;
      }
    }
    effect = updateRust(input);
  }
  if (input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) && effect.kind == XP_UI_ACTIVATE)
    haptic_feedback::touchAction(false);
  if (effect.invalidate) requestUpdate();
  applyRustEffect(effect);
}
