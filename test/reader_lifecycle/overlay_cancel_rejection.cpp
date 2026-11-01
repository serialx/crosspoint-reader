#include <string>
#include <vector>

#include "overlay_actions_session_fixture.h"

extern "C" void xp_epub_test_overlay_cancel_rejected(void*, const XpUiInput*);
extern "C" uint8_t xp_epub_test_overlay_consume_rejected(void*);
static unsigned modelDepth, renderDepth;
static bool popupActive;
static int native, toolbar, popup;
static std::vector<std::string> events;
static XpUiInput frame;
static void event(const char* name) {
  assert(!modelDepth);
  events.emplace_back(name);
}
static void locked(const char* name) {
  assert(renderDepth == 1);
  event(name);
}
static void unlocked(const char* name) {
  assert(!renderDepth);
  event(name);
}
static const XpAppRuntime runtime = [] {
  auto value = overlayUnexpectedRuntime();
  value.model_lock = [] {
    assert(!modelDepth);
    ++modelDepth;
  };
  value.model_unlock = [] {
    assert(modelDepth == 1);
    --modelDepth;
  };
  value.render_lock = []() -> void* {
    unlocked("lock");
    ++renderDepth;
    return &native;
  };
  value.render_unlock = [](void* token) {
    assert(token == &native);
    locked("unlock");
    --renderDepth;
  };
  return value;
}();
static const XpUiReaderRenderServices renderServices{nullptr, &OverlayUnexpected<uint32_t (*)(void*, uint8_t)>::call,
                                                     [](void*, const XpUiReaderRenderOp* operation) -> uint32_t {
                                                       assert(operation->kind == XP_UI_READER_RENDER_STORE_BW);
                                                       locked("store");
                                                       return 1;
                                                     }};
static const XpEpubLeaves leaves = [] {
  auto value = overlayUnexpectedLeaves();
  value.input_snapshot = [](XpUiInput* out) {
    unlocked("snapshot");
    *out = frame;
  };
  value.popup_active = [](void* p) -> uint8_t {
    assert(p == &popup);
    unlocked("active");
    return popupActive;
  };
  value.popup_dismiss = [](void* p) {
    assert(p == &popup);
    unlocked("dismiss");
    popupActive = false;
  };
  value.toolbar_close = [](void* p) {
    assert(p == &toolbar);
    locked("close");
  };
  value.render_geometry = [](XpEpubGeometry* out) {
    unlocked("geometry");
    *out = {};
    out->xteink_panel = 1;
  };
  value.render_restore_bw = [](uint8_t resync) {
    assert(!resync);
    locked("restore");
  };
  value.render_services = [](void* root) {
    assert(root == &native && renderDepth == 1);
    return &renderServices;
  };
  return value;
}();
static const XpAppResources resources = overlayUnexpectedResources();
static XpEpubTestOverlaySeed seedFor(uint8_t mode, bool active, bool stored, bool failed) {
  XpEpubTestOverlaySeed seed{};
  seed.native = &native;
  seed.toolbar = &toolbar;
  seed.popup = &popup;
  seed.overlay = mode;
  seed.stored = stored;
  seed.failed = failed;
  seed.focus = 2;
  seed.index = 7;
  seed.home_reset = [](void*) { unlocked("home"); };
  seed.current_toc = &OverlayUnexpected<decltype(seed.current_toc)>::call;
  seed.build_more = &OverlayUnexpected<decltype(seed.build_more)>::call;
  seed.paint = [](void*) { locked("paint"); };
  seed.push = [](void*) { locked("push"); };
  seed.a4 = [](void*, uint8_t value) {
    assert(!value);
    unlocked("a4");
  };
  seed.xteink = [](void*, uint8_t value) {
    assert(value);
    unlocked("xteink");
  };
  seed.settle = [](void*) { locked("settle"); };
  seed.discard = [](void*) { locked("discard"); };
  seed.update = [](void*) { event("update"); };
  popupActive = active;
  return seed;
}
int main() {
  unsigned cases = 0;
  for (uint8_t mode = 0; mode <= 4; ++mode) {
    for (bool active : {false, true}) {
      for (bool stored : {false, true}) {
        for (bool failed : {false, true}) {
          auto seed = seedFor(mode, active, stored, failed);
          void* owner = xp_epub_test_overlay_create(&seed, &leaves, &runtime, &resources);
          assert(owner);
          events.clear();
          XpUiInput input{};
          input.pressed = XP_UI_CONFIRM | XP_UI_RIGHT;
          input.released = XP_UI_CONFIRM;
          input.touch_flags = XP_UI_TOUCH_RELEASED;
          input.swipe = XP_UI_SWIPE_DOWN;
          xp_epub_test_overlay_cancel_rejected(owner, &input);
          assert(events.empty());  // Only a Back release may escape the failed-scene gate.
          input.released |= XP_UI_BACK;
          if (failed) {
            frame = input;
            assert(xp_epub_test_overlay_consume_rejected(owner));
          } else {
            xp_epub_test_overlay_cancel_rejected(owner, &input);
          }
          XpEpubTestOverlayObservation observed{};
          xp_epub_test_overlay_observe(owner, &observed);
          std::vector<std::string> expected;
          if (!mode) {
            assert(observed.overlay == 0 && observed.toolbar);
          } else if (active) {
            expected = {"active", "dismiss", "active", "lock", "settle"};
            if (stored) {
              expected.insert(expected.end(), {"restore", "store", "paint", "push"});
            } else {
              expected.push_back("update");
            }
            expected.push_back("unlock");
            assert(observed.overlay == mode && observed.toolbar && !popupActive);
          } else if (mode == 1) {
            expected = {"active", "home",   "dismiss", "lock",    "close",  "unlock", "geometry",
                        "a4",     "xteink", "lock",    "discard", "unlock", "update"};
            assert(observed.overlay == 0 && !observed.toolbar && !observed.stored && "failed Back must close toolbar");
          } else {
            expected = {"active"};
            if (stored) {
              expected.insert(expected.end(), {"lock", "settle", "restore", "store", "unlock", "lock", "settle",
                                               "paint", "push", "unlock"});
            } else {
              expected.push_back("update");
            }
            assert(observed.overlay == 1 && observed.toolbar && "failed Back must return to toolbar");
          }
          if (failed) expected.insert(expected.begin(), {"snapshot", "update"});
          assert(events == expected && "Back cancellation order");
          assert(observed.failed == failed && observed.focus == 2 && observed.index == 7);
          assert(!modelDepth && !renderDepth);
          xp_epub_test_overlay_destroy(owner);
          ++cases;
        }
      }
    }
  }
  assert(cases == 40);
}
