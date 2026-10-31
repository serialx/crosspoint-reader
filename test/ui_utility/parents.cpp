#include <xp_ui_utility.h>

#include <cassert>
#include <memory>
#include <new>
#include <utility>

#include "RustActivityAdapter.h"
#include "components/UiAppHelpers.h"

bool allocationFails = false;
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (allocationFails) return {};
  return std::unique_ptr<T>(new (std::nothrow) std::remove_extent_t<T>[count]);
}
class UtilityActivity : public RustActivityAdapter {
 public:
  Publication publication{};
  std::unique_ptr<uint8_t[]> arena;
  uint16_t commandCapacity = 10, hitCapacity = 3;
  void onEnter() override;
  XpUiUtilityState state{};
  XpUiUtilityFeedback feedback{};
  XpUiUtilityEffect pendingEffect{};
  UtilityActivity(GfxRenderer& r, MappedInputManager& i, int kind) : RustActivityAdapter("utility", r, i) {
    state.kind = kind;
    state.minimum = 0;
    state.maximum = 100;
    state.value = 42;
    state.small_step = 1;
    state.large_step = 10;
    assert(xp_ui_utility_init(&state));
  }
  XpUiInterest rustInterest(uint32_t now) const override { return xp_ui_utility_interest(&state, now); }
  XpUiEffect updateRust(const XpUiInput& i) override {
    pendingEffect = xp_ui_utility_update(&state, &i, nullptr, &feedback);
    return pendingEffect.navigation;
  }
  void applyRustEffect(const XpUiEffect&) override { applyUtilityEffect(pendingEffect); }
  virtual void applyUtilityEffect(const XpUiUtilityEffect&);
};
class IntervalSelectionActivity : public UtilityActivity {
 public:
  IntervalSelectionActivity(GfxRenderer& r, MappedInputManager& i) : UtilityActivity(r, i, XP_UI_UTILITY_INTERVAL) {}
  void applyUtilityEffect(const XpUiUtilityEffect&) override;
};
// PRODUCTION_EFFECTS
struct CppParent : Activity {
  using Activity::Activity;
};
struct RustParent : RustActivityAdapter {
  using RustActivityAdapter::RustActivityAdapter;
};
// The production manager delivery sequence, after the child has been popped.
void deliver(Activity* currentActivity, const ActivityResult& pendingResult) {
  struct {
    void unlock() {}
  } lock;
  // PRODUCTION_DELIVERY
}
int main() {
  GfxRenderer renderer;
  MappedInputManager input;
  CppParent cpp("cpp parent", renderer, input);
  RustParent rust("rust parent", renderer, input);
  for (Activity* parent : {static_cast<Activity*>(&cpp), static_cast<Activity*>(&rust)}) {
    UtilityActivity failed(renderer, input, XP_UI_UTILITY_CONFIRMATION);
    allocationFails = true;
    failed.onEnter();
    allocationFails = false;
    assert(failed.finished && failed.result.isCancelled && !failed.publication.ready && !failed.arena);
    int failures = 0;
    parent->resultHandler = [&](const ActivityResult& r) {
      assert(r.isCancelled);
      ++failures;
    };
    deliver(parent, failed.result);
    assert(failures == 1);
    for (int kind = 0; kind < 5; ++kind)
      for (bool confirm : {false, true}) {
        UtilityActivity generic(renderer, input, kind);
        IntervalSelectionActivity interval(renderer, input);
        UtilityActivity& child = kind == XP_UI_UTILITY_INTERVAL ? interval : generic;
        child.state.selected = 1;
        // A long press that opened a child suppresses its inherited release in the shared input owner.
        input.suppressNextRelease(MappedInputManager::Button::Confirm);
        input.released = 1u << unsigned(MappedInputManager::Button::Confirm);
        if (!input.consumeSuppressedRelease()) child.loop();
        assert(!child.finished);
        input.released =
            1u << unsigned(confirm ? MappedInputManager::Button::Confirm : MappedInputManager::Button::Back);
        child.loop();
        input.released = 0;
        child.loop();
        if (kind == XP_UI_UTILITY_FRONTLIGHT && confirm) {
          assert(!child.finished);
          continue;
        }
        assert(child.finished);
        int calls = 0;
        parent->resultHandler = [&](const ActivityResult& r) {
          ++calls;
          assert(r.isCancelled == (!confirm && kind != XP_UI_UTILITY_QR && kind != XP_UI_UTILITY_FRONTLIGHT));
          if (kind == XP_UI_UTILITY_INTERVAL && confirm) assert(std::get<IntervalResult>(r.data).value == 42);
          // Re-entrant parent handlers may install a new child handler.
          parent->resultHandler = [](const ActivityResult&) {};
        };
        deliver(parent, child.result);
        assert(calls == 1);
        assert(parent->resultHandler);
      }
  }
}
