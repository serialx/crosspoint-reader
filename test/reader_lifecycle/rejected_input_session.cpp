#include <initializer_list>

#include "overlay_actions_session_fixture.h"

extern "C" void xp_epub_test_overlay_reject(void*);
extern "C" void xp_epub_test_overlay_publish(void*);
extern "C" uint8_t xp_epub_test_overlay_consume_rejected(void*);
static unsigned depth, snapshots, updates;
static void* owner;
static XpUiInput frame;
static bool recoverDuringSample;
static const XpAppRuntime runtime = [] {
  auto value = overlayUnexpectedRuntime();
  value.model_lock = [] {
    assert(!depth);
    ++depth;
  };
  value.model_unlock = [] {
    assert(depth == 1);
    --depth;
  };
  return value;
}();
static const XpEpubLeaves leaves = [] {
  auto value = overlayUnexpectedLeaves();
  value.input_snapshot = [](XpUiInput* out) {
    assert(!depth);
    ++snapshots;
    *out = frame;
    if (recoverDuringSample) xp_epub_test_overlay_publish(owner);
  };
  return value;
}();
static const XpAppResources resources = overlayUnexpectedResources();
static void create() {
  XpEpubTestOverlaySeed seed{};
  seed.home_reset = &OverlayUnexpected<decltype(seed.home_reset)>::call;
  seed.current_toc = &OverlayUnexpected<decltype(seed.current_toc)>::call;
  seed.build_more = &OverlayUnexpected<decltype(seed.build_more)>::call;
  seed.paint = &OverlayUnexpected<decltype(seed.paint)>::call;
  seed.push = &OverlayUnexpected<decltype(seed.push)>::call;
  seed.a4 = &OverlayUnexpected<decltype(seed.a4)>::call;
  seed.xteink = &OverlayUnexpected<decltype(seed.xteink)>::call;
  seed.settle = &OverlayUnexpected<decltype(seed.settle)>::call;
  seed.discard = &OverlayUnexpected<decltype(seed.discard)>::call;
  seed.update = [](void*) {
    assert(!depth);
    ++updates;
  };
  owner = xp_epub_test_overlay_create(&seed, &leaves, &runtime, &resources);
  assert(owner);
  snapshots = updates = 0;
  frame = {};
  recoverDuringSample = false;
}
static void consume(bool expected, unsigned samples, unsigned invalidations) {
  assert(bool(xp_epub_test_overlay_consume_rejected(owner)) == expected && "rejected input admission");
  assert(snapshots == samples && updates == invalidations && "rejected input effects");
}
int main() {
  for (unsigned bit = 0; bit < 15; ++bit) {
    create();
    consume(false, 0, 0);  // The clear gate must not probe input.
    xp_epub_test_overlay_reject(owner);
    frame.pressed = frame.held = 1u << bit;
    consume(true, 1, 1);
    xp_epub_test_overlay_publish(owner);
    frame.pressed = 0;
    consume(true, 2, 1);
    frame.held = 0;
    frame.released = 1u << bit;
    consume(true, 3, 1);
    consume(false, 3, 1);  // Suppression ends after release, without another sample.
    xp_epub_test_overlay_destroy(owner);
  }
  for (uint8_t flags : {XP_UI_TOUCH_PRESSED, XP_UI_TOUCH_HELD}) {
    create();
    xp_epub_test_overlay_reject(owner);
    frame.touch_flags = flags;
    consume(true, 1, 1);
    xp_epub_test_overlay_publish(owner);
    frame.touch_flags = XP_UI_TOUCH_HELD;
    consume(true, 2, 1);
    frame.touch_flags = XP_UI_TOUCH_RELEASED;
    frame.swipe = XP_UI_SWIPE_RIGHT;
    consume(true, 3, 1);
    consume(false, 3, 1);
    xp_epub_test_overlay_destroy(owner);
  }
  for (bool recoverInSample : {false, true}) {
    create();
    xp_epub_test_overlay_reject(owner);
    if (!recoverInSample) xp_epub_test_overlay_publish(owner);
    recoverDuringSample = recoverInSample;
    frame.pressed = XP_UI_CONFIRM;
    frame.released = XP_UI_BACK;
    frame.swipe = XP_UI_SWIPE_DOWN;
    consume(true, 1, 0);  // Publication can beat the loop; suppress one fresh gesture.
    consume(false, 1, 0);
    xp_epub_test_overlay_destroy(owner);
  }
  create();
  xp_epub_test_overlay_reject(owner);
  consume(true, 1, 1);
  consume(true, 2, 2);  // Failed blank frames still request a retry.
  xp_epub_test_overlay_publish(owner);
  consume(false, 2, 2);
  xp_epub_test_overlay_destroy(owner);
}
