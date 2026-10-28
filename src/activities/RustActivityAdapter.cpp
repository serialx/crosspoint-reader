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
    if (auto* publication = rustPublication()) publication->ready = false;
    return false;
  }
  rustPublished();
  if (auto* publication = rustPublication()) {
    publication->scene = scene;
    publication->scene.commands = nullptr;
    publication->scene.command_count = 0;
    publication->scene.text = nullptr;
    publication->scene.text_len = 0;
    publication->ready = true;
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
    if (auto* publication = rustPublication()) publication->ready = false;
    LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
    return;
  }
  paintRustScene(scene);
}

void RustActivityAdapter::loop() {
  XpUiInput input{};
  constexpr MappedInputManager::Button buttons[] = {
      MappedInputManager::Button::Back, MappedInputManager::Button::Confirm, MappedInputManager::Button::NavNext,
      MappedInputManager::Button::NavPrevious};
  for (unsigned i = 0; i < 4; ++i) {
    if (mappedInput.wasPressed(buttons[i])) input.pressed |= 1u << i;
    if (mappedInput.wasReleased(buttons[i])) input.released |= 1u << i;
    if (mappedInput.isPressed(buttons[i])) input.held |= 1u << i;
  }
  input.now_ms = millis();
  input.held_ms = mappedInput.getHeldTime();
  const auto touch = touchSnapshotFrom(mappedInput);
  input.touch_x = touch.touchX;
  input.touch_y = touch.touchY;
  input.touch_flags = (touch.touchPressed ? 1 : 0) | (touch.touchReleased ? 2 : 0) | (touch.touchHeld ? 4 : 0);
  switch (mappedInput.wasSwipe()) {
    case MappedInputManager::SwipeDir::Up:
      input.swipe = 1;
      break;
    case MappedInputManager::SwipeDir::Down:
      input.swipe = -1;
      break;
    case MappedInputManager::SwipeDir::None:
    case MappedInputManager::SwipeDir::Left:
    case MappedInputManager::SwipeDir::Right:
      break;
  }
  XpUiEffect effect{};
  {
    RenderLock lock(*this);
    const auto* publication = rustPublication();
    if (publication && publication->ready && (input.touch_flags & 3)) {
      XpUiInteraction hit{};
      if (xp_ui_hit(&publication->scene, input.touch_x, input.touch_y, 1, &hit)) {
        input.action = hit.action;
        input.value = hit.value;
      }
    }
    effect = updateRust(input);
  }
  if (input.action && (input.touch_flags & 2) && effect.kind == XP_UI_ACTIVATE) haptic_feedback::touchAction(false);
  if (effect.invalidate) requestUpdate();
  applyRustEffect(effect);
}
