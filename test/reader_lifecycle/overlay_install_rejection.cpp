#include <atomic>
#include <chrono>
#include <future>
#include <mutex>

#include "overlay_actions_session_fixture.h"

extern "C" void xp_epub_test_overlay_reject(void*);
using namespace std::chrono_literals;
static std::mutex modelMutex, renderMutex;
static thread_local unsigned modelDepth, renderDepth;
static std::promise<void> waiting;
static std::atomic<unsigned> opened, closed, updates, resets;
static void* lockRender() {
  assert(!modelDepth && !renderDepth);
  waiting.set_value();
  renderMutex.lock();
  ++renderDepth;
  return &renderMutex;
}
static const XpAppRuntime runtime = [] {
  auto value = overlayUnexpectedRuntime();
  value.model_lock = [] {
    assert(!modelDepth);
    modelMutex.lock();
    ++modelDepth;
  };
  value.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
    modelMutex.unlock();
  };
  value.render_lock = lockRender;
  value.render_unlock = [](void* token) {
    assert(!modelDepth && renderDepth == 1 && token == &renderMutex);
    --renderDepth;
    renderMutex.unlock();
  };
  return value;
}();
static const XpUiReaderInputServices input{nullptr, nullptr,
                                           [](void*, uint8_t query) -> uint32_t {
                                             assert(query == XP_UI_READER_INPUT_HAS_TOUCH);
                                             return 0;
                                           },
                                           nullptr};
static const XpEpubLeaves leaves = [] {
  auto value = overlayUnexpectedLeaves();
  value.toolbar_open = []() -> void* {
    assert(!renderDepth && !modelDepth);
    ++opened;
    return new int(7);
  };
  value.toolbar_close = [](void* p) {
    assert(renderDepth == 1 && !modelDepth);
    ++closed;
    delete static_cast<int*>(p);
  };
  value.toolbar_begin = [](void*) { assert(!renderDepth && !modelDepth); };
  value.input_services = [] { return &input; };
  return value;
}();
static const XpAppResources resources = overlayUnexpectedResources();
int main() {
  int native;
  XpEpubTestOverlaySeed seed{};
  seed.native = &native;
  seed.focus = 2;
  seed.index = 9;
  seed.counter = 3;
  seed.hold = 1;
  seed.stored = 1;
  seed.home_reset = [](void*) {
    assert(!modelDepth && !renderDepth);
    ++resets;
  };
  seed.current_toc = &OverlayUnexpected<decltype(seed.current_toc)>::call;
  seed.build_more = &OverlayUnexpected<decltype(seed.build_more)>::call;
  seed.paint = &OverlayUnexpected<decltype(seed.paint)>::call;
  seed.push = &OverlayUnexpected<decltype(seed.push)>::call;
  seed.a4 = &OverlayUnexpected<decltype(seed.a4)>::call;
  seed.xteink = &OverlayUnexpected<decltype(seed.xteink)>::call;
  seed.settle = &OverlayUnexpected<decltype(seed.settle)>::call;
  seed.discard = &OverlayUnexpected<decltype(seed.discard)>::call;
  seed.update = [](void*) {
    assert(!modelDepth && !renderDepth);
    ++updates;
  };
  void* owner = xp_epub_test_overlay_create(&seed, &leaves, &runtime, &resources);
  assert(owner);
  renderMutex.lock();
  ++renderDepth;
  auto opening = std::async(std::launch::async, [=] { xp_epub_test_overlay_open(owner, 1); });
  assert(waiting.get_future().wait_for(5s) == std::future_status::ready);
  XpEpubTestOverlayObservation before{};
  xp_epub_test_overlay_observe(owner, &before);
  assert(before.overlay == 0 && !before.toolbar && before.focus == 2 && before.index == 9 && !before.failed);
  xp_epub_test_overlay_reject(owner);
  --renderDepth;
  renderMutex.unlock();
  opening.get();
  XpEpubTestOverlayObservation after{};
  xp_epub_test_overlay_observe(owner, &after);
  assert(after.overlay == before.overlay && !after.toolbar && "late rejection must not open the panel");
  assert(after.focus == before.focus && after.index == before.index && after.cursor == before.cursor);
  assert(after.stored == before.stored && after.counter == before.counter && after.failed && !after.hold);
  assert(opened == 1 && closed == 1 && resets == 1 && updates == 1);
  xp_epub_test_overlay_destroy(owner);
}
