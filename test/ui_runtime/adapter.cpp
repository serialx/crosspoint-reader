#include <cassert>
#include <future>
#include <vector>

#include "RustActivityAdapter.h"

class List final : public RustActivityAdapter {
 public:
  XpUiListState state{0, 0, 3, 3, 10, 0, -1, 0, 1, {}};
  std::vector<XpUiInput> inputs;
  List(GfxRenderer& renderer, MappedInputManager& input) : RustActivityAdapter("test", renderer, input) {}
  bool buildOk = true;
  unsigned activations = 0, cancellations = 0;
  void applyRustEffect(const XpUiEffect& effect) override {
    activations += effect.kind == XP_UI_ACTIVATE;
    cancellations += effect.kind == XP_UI_CANCEL;
  }
  bool buildRustScene(XpUiScene& scene) override {
    scene.refresh = XP_UI_FAST;
    return buildOk;
  }
  XpUiInterest rustInterest(uint32_t nowMs) const override { return xp_ui_list_interest(&state, nowMs); }
  XpUiEffect updateRust(const XpUiInput& input) override {
    inputs.push_back(input);
    return xp_ui_list_update(&state, &input, 10, 1);
  }
};
class Probe final : public RustActivityAdapter {
 public:
  XpUiInterest interest{};
  XpUiInput last{};
  unsigned count = 0;
  Probe(GfxRenderer& r, MappedInputManager& i) : RustActivityAdapter("probe", r, i) {}
  XpUiInterest rustInterest(uint32_t) const override { return interest; }
  XpUiEffect updateRust(const XpUiInput& input) override {
    last = input;
    ++count;
    return {};
  }
};

class FailureProbe final : public RustActivityAdapter {
 public:
  using RustActivityAdapter::consumeRejectedInput;
  using RustActivityAdapter::publishRustScene;
  using RustActivityAdapter::rejectRustScene;
  bool rejectBuild = true;
  unsigned failures = 0, published = 0;
  FailureProbe(GfxRenderer& r, MappedInputManager& i) : RustActivityAdapter("failure", r, i) {}
  bool buildRustScene(XpUiScene&) override { return !rejectBuild; }
  void rustSceneFailed() override { ++failures; }
  void rustPublished() override { ++published; }
};
static void recoveryBeforeInput() {
  for (bool touch : {false, true}) {
    GfxRenderer renderer;
    MappedInputManager input;
    FailureProbe probe(renderer, input);
    assert(!probe.consumeRejectedInput());
    assert(input.heldProbes == 0);
    probe.rejectRustScene();
    probe.publishRustScene({});
    // A fresh gesture in this narrow window is consumed once, even though the
    // recovered scene is valid. The next gesture must work normally.
    if (touch) {
      input.touchFlags = XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_HELD;
    } else {
      input.pressed = input.held = 1u << unsigned(MappedInputManager::Button::Confirm);
    }
    assert(probe.consumeRejectedInput());
    input.pressed = input.held = 0;
    input.released = touch ? 0 : 1u << unsigned(MappedInputManager::Button::Confirm);
    input.touchFlags = touch ? XP_UI_TOUCH_RELEASED : 0;
    assert(probe.consumeRejectedInput());
    input = {};
    assert(!probe.consumeRejectedInput());
    if (touch)
      input.touchFlags = XP_UI_TOUCH_RELEASED;
    else
      input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    assert(!probe.consumeRejectedInput());
  }
}
static void rejectionRecovery() {
  for (bool published : {false, true}) {
    for (bool buildFailure : {false, true}) {
      GfxRenderer renderer;
      renderer.finishRefresh = true;
      MappedInputManager input;
      List list(renderer, input);
      const auto paint = [&] {
        RenderLock lock;
        list.render(std::move(lock));
      };
      // Initial input retains its original behavior, even before any paint.
      input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
      list.loop();
      assert(list.activations == 1);
      input = {};
      if (published) paint();
      list.buildOk = !buildFailure;
      renderer.paintOk = buildFailure;
      paint();
      const auto selected = list.state.selected;
      input.pressed = input.held = 1u << unsigned(MappedInputManager::Button::NavNext);
      list.loop();
      input.pressed = 0;
      input.held = 1u << unsigned(MappedInputManager::Button::Confirm);
      input.heldTime = 800;
      list.loop();
      input.released = input.held;
      input.held = 0;
      list.loop();
      input = {};
      input.touchFlags = XP_UI_TOUCH_RELEASED;
      list.loop();
      assert(list.activations == 1 && list.state.selected == selected);
      input = {};
      input.released = 1u << unsigned(MappedInputManager::Button::Back);
      list.loop();
      assert(list.cancellations == 1);
      // A held gesture crossing recovery must consume its release, too.
      input = {};
      input.pressed = input.held = 1u << unsigned(MappedInputManager::Button::Confirm);
      list.loop();
      list.buildOk = renderer.paintOk = true;
      paint();
      input.pressed = input.held = 0;
      input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
      list.loop();
      assert(list.activations == 1);
      input = {};
      list.loop();
      input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
      list.loop();
      assert(list.activations == 2);
    }
  }
}

int main() {
  recoveryBeforeInput();
  rejectionRecovery();
  GfxRenderer renderer;
  MappedInputManager input;
  List list(renderer, input);
  std::thread render([&] {
    RenderLock lock;
    list.render(std::move(lock));
  });
  while (!renderer.refreshing) std::this_thread::yield();
  auto polling = std::async(std::launch::async, [&] {
    for (int edge = 0; edge < 4; ++edge) {
      testNow = 100 + edge * 50;
      input.pressed = edge % 2 == 0 ? 1u << unsigned(MappedInputManager::Button::NavNext) : 0;
      input.released = edge % 2 == 1 ? 1u << unsigned(MappedInputManager::Button::NavNext) : 0;
      input.held = input.pressed;
      list.loop();
    }
  });
  const bool ingested = polling.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
  renderer.finishRefresh = true;
  render.join();
  polling.get();
  assert(ingested && "input ingestion waited on synchronous refresh");
  assert(list.inputs.size() == 4 && list.state.selected == 2);
  // Held frames keep the original 501 ms repeat threshold after rapid releases.
  input.pressed = input.released = 0;
  input.held = 1u << unsigned(MappedInputManager::Button::NavNext);
  input.heldTime = 500;
  testNow = 1000;
  list.loop();
  assert(list.inputs.size() == 4);
  input.heldTime = 501;
  testNow = 1001;
  list.loop();
  assert(list.state.selected == 3);
  testNow = 1501;
  list.loop();
  assert(list.inputs.size() == 5);
  testNow = 1502;
  list.loop();
  assert(list.state.selected == 6);
  Probe probe(renderer, input);
  input = {};
  probe.interest.pressed = probe.interest.released = probe.interest.held = 0x7FFF;
  probe.interest.touch_flags = XP_UI_TOUCH_HELD;
  probe.interest.swipes = (1 << XP_UI_SWIPE_LEFT) | (1 << XP_UI_SWIPE_RIGHT);
  input.pressed = input.released = input.held = 0x7FFF;
  probe.loop();
  assert(probe.last.pressed == 0x7FFF && probe.last.released == 0x7FFF && probe.last.held == 0x7FFF);
  input = {};
  input.pressed = 1u << unsigned(MappedInputManager::Button::ScreenLeft);
  probe.loop();
  assert(probe.last.pressed == XP_UI_SCREEN_LEFT);
  input.pressed = 1u << unsigned(MappedInputManager::Button::Left);
  probe.loop();
  assert(probe.last.pressed == XP_UI_LEFT);
  input = {};
  input.touchFlags = XP_UI_TOUCH_HELD;
  probe.loop();
  assert(probe.last.touch_flags == XP_UI_TOUCH_HELD);
  input = {};
  input.swipe = MappedInputManager::SwipeDir::Left;
  probe.loop();
  assert(probe.last.swipe == XP_UI_SWIPE_LEFT);
  input.swipe = MappedInputManager::SwipeDir::Right;
  probe.loop();
  assert(probe.last.swipe == XP_UI_SWIPE_RIGHT);
  input = {};
  probe.interest = {};
  probe.interest.deadline_enabled = 1;
  probe.interest.deadline_ms = 2000;
  const auto before = probe.count;
  testNow = 1999;
  probe.loop();
  assert(probe.count == before);
  testNow = 2000;
  probe.loop();
  assert(probe.count == before + 1);
  FailureProbe failure(renderer, input);
  failure.render(RenderLock{});
  assert(failure.failures == 1 && failure.published == 0);
  failure.rejectBuild = false;
  renderer.paintOk = false;
  failure.render(RenderLock{});
  assert(failure.failures == 2 && failure.published == 0);
  renderer.paintOk = true;
  failure.render(RenderLock{});
  assert(failure.failures == 2 && failure.published == 1);
}
