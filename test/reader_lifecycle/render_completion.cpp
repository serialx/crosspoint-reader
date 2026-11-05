#include <atomic>
#include <future>
#include <mutex>

#include "overlay_actions_session_fixture.h"

extern "C" void xp_epub_test_overlay_complete_render(void*);
extern "C" void xp_epub_test_overlay_set_mode(void*, uint8_t);
static std::mutex modelMutex;
static thread_local unsigned modelDepth, completionLocks, completionUnlocks;
static thread_local bool completing;
static std::atomic<bool> crossing, checking, attempted, writerDone;
static const XpAppRuntime runtime = [] {
  auto value = overlayUnexpectedRuntime();
  value.model_lock = [] {
    assert(!modelDepth);
    modelMutex.lock();
    ++modelDepth;
    if (completing && crossing && ++completionLocks == 1) {
      checking = true;
      while (!attempted) std::this_thread::yield();
    }
  };
  value.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
    if (completing && crossing && ++completionUnlocks == 1) {
      while (!writerDone) std::this_thread::yield();
    }
  };
  return value;
}();
static const LegacyEpubLeaves leaves = overlayLegacyLeaves();
static const XpAppResources resources = overlayUnexpectedResources();
static void* create() {
  XpEpubTestOverlaySeed seed{};
  seed.overlay = 1;
  seed.failed = 1;
  seed.home_reset = &OverlayUnexpected<decltype(seed.home_reset)>::call;
  seed.current_toc = &OverlayUnexpected<decltype(seed.current_toc)>::call;
  seed.build_more = &OverlayUnexpected<decltype(seed.build_more)>::call;
  seed.paint = &OverlayUnexpected<decltype(seed.paint)>::call;
  seed.push = &OverlayUnexpected<decltype(seed.push)>::call;
  seed.a4 = &OverlayUnexpected<decltype(seed.a4)>::call;
  seed.xteink = &OverlayUnexpected<decltype(seed.xteink)>::call;
  seed.settle = &OverlayUnexpected<decltype(seed.settle)>::call;
  seed.discard = &OverlayUnexpected<decltype(seed.discard)>::call;
  seed.update = &OverlayUnexpected<decltype(seed.update)>::call;
  return xp_epub_test_overlay_create_legacy_book(&seed, &leaves.physical, &runtime, &resources, &leaves.book);
}
static XpEpubTestOverlayObservation observe(void* owner) {
  XpEpubTestOverlayObservation value{};
  xp_epub_test_overlay_observe(owner, &value);
  return value;
}
int main() {
  for (uint8_t target : {0, 1, 3}) {
    void* owner = create();
    assert(owner);
    std::promise<void> finishing, proceed;
    auto resume = proceed.get_future();
    auto render = std::async(std::launch::async, [&] {
      finishing.set_value();
      resume.wait();
      xp_epub_test_overlay_complete_render(owner);
    });
    finishing.get_future().wait();
    xp_epub_test_overlay_set_mode(owner, target);
    proceed.set_value();
    render.get();
    const auto result = observe(owner);
    assert(result.failed == (target != 0) && result.overlay == target);
    xp_epub_test_overlay_destroy(owner);
  }
  void* owner = create();
  assert(owner);
  xp_epub_test_overlay_set_mode(owner, 0);
  crossing = true;
  auto writer = std::async(std::launch::async, [&] {
    while (!checking) std::this_thread::yield();
    const bool acquired = modelMutex.try_lock();
    assert(!acquired);  // A transition cannot enter between the decision and publication.
    attempted = true;
    xp_epub_test_overlay_set_mode(owner, 3);
    assert(!observe(owner).failed && "completion must publish before model unlock");
    writerDone = true;
  });
  completing = true;
  xp_epub_test_overlay_complete_render(owner);
  completing = false;
  writer.get();
  const auto result = observe(owner);
  assert(!result.failed && result.overlay == 3);
  assert(completionLocks == 1 && completionUnlocks == 1);
  xp_epub_test_overlay_destroy(owner);
}
