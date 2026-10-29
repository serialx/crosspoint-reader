#include <xp_ui_koreader.h>

#include <cassert>
#include <future>

#include "RustActivityAdapter.h"
#define tr(name) #name
class KOReaderUiActivity : public RustActivityAdapter {
 public:
  XpKoState model{};
  bool sceneFailed = false, touchDispatched = false;
  const char* status = "";
  bool rejectBuild = false;
  unsigned effects = 0;
  XpUiEffect last{};
  KOReaderUiActivity(GfxRenderer& r, MappedInputManager& i) : RustActivityAdapter("KOReader", r, i) {
    model.phase = XP_KO_RESULT;
  }
  bool buildRustScene(XpUiScene& scene) override {
    scene.refresh = XP_UI_FAST;
    return !rejectBuild;
  }
  void rustSceneFailed() override;
  void rustPublished() override { sceneFailed = false; }
  XpUiInterest rustInterest(uint32_t) const override;
  XpUiEffect updateRust(const XpUiInput&) override;
  void publish(XpKoPhase, XpKoMessage, uint8_t, const char*);
  void markReturn();
  void applyRustEffect(const XpUiEffect&) override;
  void performEffect(const XpUiEffect& e) {
    if (e.kind) {
      ++effects;
      last = e;
    }
  }
};
#include "adapter-methods.inc"
static_assert(sizeof(XpKoState) == 12);
static_assert(sizeof(XpKoOptions) == 12);
static_assert(offsetof(XpKoOptions, local_percentage) == 0);
static_assert(offsetof(XpKoOptions, smart) == 4);
int main() {
  GfxRenderer renderer;
  MappedInputManager input;
  KOReaderUiActivity activity(renderer, input);
  std::thread painting([&] {
    RenderLock lock;
    activity.render(std::move(lock));
  });
  while (!renderer.refreshing) std::this_thread::yield();
  auto polling = std::async(std::launch::async, [&] {
    input.pressed = 1u << unsigned(MappedInputManager::Button::Confirm);
    activity.loop();
    assert(activity.effects == 0);
    input.pressed = 0;
    input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
    activity.loop();
    assert(activity.effects == 1 && activity.last.kind == XP_UI_ACTIVATE && activity.last.value == 0);
    activity.publish(XP_KO_FAILED, XP_KO_SAVE_FAILED, 0, nullptr);
    assert(activity.model.phase == XP_KO_FAILED);
  });
  const bool handled = polling.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
  renderer.finishRefresh = true;
  painting.join();
  polling.get();
  assert(handled && "KOReader input/progress waited on panel refresh");
  for (int kind : {0, 1, 2})
    for (bool painted : {false, true})
      for (bool buildFailure : {false, true}) {
        KOReaderUiActivity probe(renderer, input);
        probe.model.kind = kind;
        probe.model.phase = kind ? XP_KO_AUTHENTICATED : XP_KO_RESULT;
        XpUiInput confirm{};
        confirm.pressed = confirm.released = XP_UI_CONFIRM;
        assert(probe.updateRust(confirm).kind == XP_UI_ACTIVATE);
        if (painted) probe.render(RenderLock{});
        probe.model.touch_armed = 1;
        probe.touchDispatched = true;
        probe.rejectBuild = buildFailure;
        renderer.validScene = buildFailure;
        probe.render(RenderLock{});
        assert(probe.sceneFailed && !probe.model.touch_armed && !probe.touchDispatched);
        XpUiInput all{};
        all.pressed = all.released = all.held = 0xffff & ~XP_UI_BACK;
        all.touch_flags = XP_UI_TOUCH_PRESSED | XP_UI_TOUCH_RELEASED | XP_UI_TOUCH_HELD;
        all.action = 1;
        all.value = 1;
        all.swipe = XP_UI_SWIPE_DOWN;
        const auto rejected = probe.updateRust(all);
        assert(!rejected.kind && rejected.invalidate && probe.model.selected == 0);
        // An effect computed before the rejection cannot perform a sync/write afterward.
        if (!kind) {
          XpUiEffect queued{};
          queued.kind = XP_UI_ACTIVATE;
          probe.applyRustEffect(queued);
          assert(probe.effects == 0);
        }
        XpUiInput back{};
        back.pressed = back.released = XP_UI_BACK;
        const auto cancel = probe.updateRust(back);
        assert(cancel.kind == (kind ? XP_UI_ACTIVATE : XP_UI_CANCEL));
        if (kind) assert(cancel.value == 3);
        probe.rejectBuild = false;
        renderer.validScene = true;
        probe.render(RenderLock{});
        assert(!probe.sceneFailed && probe.updateRust(confirm).kind == XP_UI_ACTIVATE);
      }
}
