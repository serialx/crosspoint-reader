#include "RustActivityAdapter.h"

#include "components/RustInputSnapshot.h"

RustActivityAdapter::RustActivityAdapter(const char* name, GfxRenderer& renderer, MappedInputManager& input)
    : Activity(name, renderer, input) {}

RustActivityAdapter::ModelLock::ModelLock(RustActivityAdapter& owner) : mutex(owner.modelMutex) {
  taskENTER_CRITICAL(&mutex);
}

RustActivityAdapter::ModelLock::~ModelLock() { taskEXIT_CRITICAL(&mutex); }

XpUiMetrics RustActivityAdapter::rustMetrics() const {
  return {
      &renderer,
      [](void* context, int32_t font, const uint8_t* text, uint16_t length, uint8_t style) {
        const auto provider = static_cast<GfxRenderer*>(context)->textProvider();
        return xp_native_width(&provider, font, text, length, style, -1);
      },
      [](void* context, int32_t font) -> int32_t { return static_cast<GfxRenderer*>(context)->getLineHeight(font); }};
}

bool RustActivityAdapter::paintRustScene(const XpUiScene& scene, bool publish) {
  const auto resources = rustResources();
  if (!renderer.drawUiScene(scene, &resources)) {
    LOG_ERR("RUI", "Invalid scene in %s", name.c_str());
    rejectRustScene();
    return false;
  }
  if (publish) publishRustScene(scene);
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
    rejectRustScene();
    LOG_ERR("RUI", "Cannot build scene in %s", name.c_str());
    return;
  }
  paintRustScene(scene);
}

XpUiInput RustActivityAdapter::sampleRustInput() const { return rustInputSnapshot(mappedInput); }

void RustActivityAdapter::loop() {
  if (consumeRejectedInput()) return;
  auto input = sampleRustInput();
  XpUiEffect effect{};
  {
    ModelLock lock(*this);
    if (sceneFailed) return;
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
  if (rustSceneRejected()) {
    requestUpdate();
    return;
  }
  if (input.action && (input.touch_flags & XP_UI_TOUCH_RELEASED) && effect.kind == XP_UI_ACTIVATE)
    haptic_feedback::touchAction(false);
  if (effect.invalidate) requestUpdate();
  applyRustEffect(effect);
}

void RustActivityAdapter::publishRustScene(const XpUiScene& scene) {
  bool current;
  {
    ModelLock lock(*this);
    current = rustSceneCurrent();
    if (current) {
      sceneFailed = false;
      rustPublished();
      if (auto* publication = rustPublication()) {
        publication->scene = scene;
        publication->scene.commands = nullptr;
        publication->scene.command_count = 0;
        publication->scene.text = nullptr;
        publication->scene.text_len = 0;
        publication->ready = true;
      }
    } else if (auto* publication = rustPublication()) {
      publication->ready = false;
    }
  }
  if (!current) requestUpdate();
}

void RustActivityAdapter::rejectRustScene() {
  ModelLock lock(*this);
  sceneFailed = true;
  // Recovery can publish before the loop observes rejection. This may consume
  // one fresh gesture; then retain only held gestures through their release.
  suppressButtons = UINT16_MAX;
  suppressContact = true;
  if (auto* publication = rustPublication()) publication->ready = false;
  rustSceneFailed();
}

bool RustActivityAdapter::rustSceneRejected() const {
  ModelLock lock(*const_cast<RustActivityAdapter*>(this));
  return sceneFailed;
}

bool RustActivityAdapter::consumeRejectedInput() {
  {
    ModelLock lock(*this);
    if (!sceneFailed && !suppressButtons && !suppressContact) return false;
  }
  const auto input = sampleRustInput();
  bool failed, consume;
  {
    ModelLock lock(*this);
    failed = sceneFailed;
    consume = failed || (suppressButtons & (input.pressed | input.released | input.held)) ||
              (suppressContact && (input.touch_flags || input.swipe));
    if (failed) {
      suppressButtons |= input.held;
      suppressContact |= (input.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD)) != 0;
    }
    suppressButtons &= input.held;
    if (!(input.touch_flags & (XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD))) suppressContact = false;
  }
  if (failed) {
    requestUpdate();
    cancelRejectedInput(input);
  }
  return consume;
}

void RustActivityAdapter::cancelRejectedInput(const XpUiInput& input) {
  if (input.released & XP_UI_BACK) applyRustEffect(XpUiEffect{0, XP_UI_CANCEL, 0, {0, 0}});
}
