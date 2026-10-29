#include <xp_ui_settings.h>

#include <cassert>
#include <future>

#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"
#define LOG_DBG(...) ((void)0)
enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, USB_DRIVE };
struct NetworkModeResult {
  NetworkMode mode;
};
struct ActivityResult {
  bool isCancelled = false;
};
class NetworkModeSelectionActivity final : public RustActivityAdapter {
 public:
  int MENU_ITEM_COUNT;
  XpUiListState state{}, next{};
  XpUiSettingsHits hits{}, nextHits{};
  Publication publication;
  int32_t renderingScroll = 0;
  bool touchActivated = false;
  using RustActivityAdapter::rustSceneRejected;
  bool buildOkay = true;
  int completed = 0, mode = -1;
  bool cancelled = false;
  NetworkModeSelectionActivity(GfxRenderer& r, MappedInputManager& i, int count)
      : RustActivityAdapter("NetworkModeSelection", r, i), MENU_ITEM_COUNT(count) {
    xp_ui_settings_list_init(&state, 0);
  }
  void setResult(NetworkModeResult result) { mode = static_cast<int>(result.mode); }
  void setResult(ActivityResult result) { cancelled = result.isCancelled; }
  void finish() { ++completed; }
  void rustSceneFailed() override;
  void onModeSelected(NetworkMode);
  void onCancel();
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void applyRustEffect(const XpUiEffect&) override;
  void rustPublished() override;
  Publication* rustPublication() override { return &publication; }
  bool buildRustScene(XpUiScene& scene) override {
    scene.refresh = XP_UI_FAST;
    return buildOkay;
  }
};
// PRODUCTION_METHODS
int main() {
  for (int count : {3, 4}) {
    GfxRenderer renderer;
    MappedInputManager input;
    NetworkModeSelectionActivity initial(renderer, input, count);
    input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    initial.loop();
    assert(initial.completed == 1 && initial.mode == 0 && !initial.cancelled);
    input = {};
    NetworkModeSelectionActivity cancel(renderer, input, count);
    input.released = 1u << unsigned(MappedInputManager::Button::Back);
    cancel.loop();
    assert(cancel.completed == 1 && cancel.cancelled);
    input = {};
    NetworkModeSelectionActivity touch(renderer, input, count);
    touch.publication.ready = true;
    touch.hits.first_row = {0, 0, 80, 30};
    touch.hits.first_index = 2;
    touch.hits.heights[0] = 30;
    touch.hits.count = 1;
    touch.hits.uniform = 1;
    input.touchFlags = XP_UI_TOUCH_PRESSED;
    touch.loop();
    assert(touch.completed == 0);
    input.touchFlags = XP_UI_TOUCH_RELEASED;
    touch.loop();
    assert(touch.completed == 1 && touch.mode == 2);
    input = {};
    renderer.finishRefresh = true;
    for (bool firstPublished : {false, true}) {
      for (bool buildFailure : {false, true}) {
        NetworkModeSelectionActivity failure(renderer, input, count);
        failure.next = failure.state;
        auto paint = [&] {
          RenderLock lock;
          failure.render(std::move(lock));
        };
        if (firstPublished) paint();
        input.held = 1u << unsigned(MappedInputManager::Button::Confirm);
        input.touchFlags = XP_UI_TOUCH_HELD;
        failure.loop();
        failure.buildOkay = !buildFailure;
        renderer.acceptScene = buildFailure;
        paint();
        assert(failure.rustSceneRejected() && !failure.publication.ready);
        const auto selected = failure.state.selected;
        input.pressed = 1u << unsigned(MappedInputManager::Button::NavNext);
        input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
        input.swipe = MappedInputManager::SwipeDir::Down;
        failure.loop();
        assert(failure.completed == 0 && failure.state.selected == selected);
        input = {};
        input.released = 1u << unsigned(MappedInputManager::Button::Back);
        failure.loop();
        assert(failure.completed == 1 && failure.cancelled);
        failure.completed = 0;
        input = {};
        input.pressed = input.held = 1u << unsigned(MappedInputManager::Button::Confirm);
        input.touchFlags = XP_UI_TOUCH_PRESSED;
        failure.loop();
        failure.buildOkay = renderer.acceptScene = true;
        failure.next = failure.state;
        paint();
        assert(!failure.rustSceneRejected() && failure.publication.ready);
        failure.loop();
        assert(failure.completed == 0);
        input.pressed = input.held = 0;
        input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
        input.touchFlags = XP_UI_TOUCH_RELEASED;
        failure.loop();
        assert(failure.completed == 0);
        input = {};
        input.pressed = input.held = 1u << unsigned(MappedInputManager::Button::Confirm);
        failure.loop();
        input.pressed = input.held = 0;
        input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
        failure.loop();
        assert(failure.completed == 1 && failure.mode == selected);
        failure.completed = 0;
        failure.hits.first_row = {0, 0, 80, 30};
        failure.hits.first_index = 2;
        failure.hits.heights[0] = 30;
        failure.hits.count = 1;
        failure.hits.uniform = 1;
        input = {};
        input.touchFlags = XP_UI_TOUCH_PRESSED;
        failure.loop();
        assert(failure.completed == 0);
        input.touchFlags = XP_UI_TOUCH_RELEASED;
        failure.loop();
        assert(failure.completed == 1 && failure.mode == 2);
        input = {};
      }
    }
    input = {};
    renderer.finishRefresh = false;
    renderer.refreshing = false;
    NetworkModeSelectionActivity busy(renderer, input, count);
    busy.next = busy.state;
    std::thread render([&] {
      RenderLock lock;
      busy.render(std::move(lock));
    });
    while (!renderer.refreshing) std::this_thread::yield();
    auto poll = std::async(std::launch::async, [&] {
      input.pressed = input.held = 1u << unsigned(MappedInputManager::Button::NavNext);
      testNow = 100;
      busy.loop();
      input.pressed = input.held = 0;
      input.released = 1u << unsigned(MappedInputManager::Button::NavNext);
      testNow = 150;
      busy.loop();
      input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
      testNow = 200;
      busy.loop();
    });
    const bool handled = poll.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
    renderer.finishRefresh = true;
    render.join();
    poll.get();
    assert(handled && busy.completed == 1 && busy.mode == 1);
  }
}
