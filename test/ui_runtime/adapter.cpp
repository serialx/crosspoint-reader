#include <cassert>
#include <future>
#include <vector>

#include "RustActivityAdapter.h"

class List final : public RustActivityAdapter {
 public:
  XpUiListState state{0, 0, 3, 3, 10, 0, -1, 0, 1, {}};
  std::vector<XpUiInput> inputs;
  List(GfxRenderer& renderer, MappedInputManager& input) : RustActivityAdapter("test", renderer, input) {}
  bool buildRustScene(XpUiScene& scene) override {
    scene.refresh = XP_UI_FAST;
    return true;
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

int main() {
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
}
