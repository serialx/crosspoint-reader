#pragma once
#include <string_view>

#include "../reader_lifecycle/legacy_page_fixture.h"
#include "owner_tables.h"

static auto legacyRender = unexpectedLegacyPageServices();

struct XpEpubTestHelpersSeed {
  void* native;
  void* popup;
  int32_t refresh;
  uint8_t stored;
  uint8_t pending;
};
extern "C" {
void* xp_epub_test_helpers_create_legacy_render(const XpEpubTestHelpersSeed*, const XpEpubLeaves*, const XpAppRuntime*,
                                                const XpAppResources*, const XpEpubLegacyPageServices*);
void xp_epub_test_helpers_destroy(void*);
uint8_t xp_epub_test_build_room(void*);
}
namespace build_room_fixture {
struct Calls {
  void* context;
  uint32_t (*status)(void*);
  uint32_t (*now)(void*);
  void (*sleep)(void*, uint32_t);
  unsigned releases = 0, strips = 0;
};
inline Calls* active;
inline void physical() { assert(active && retirement_fixture::modelDepth == 0); }
}  // namespace build_room_fixture

template <typename Status, typename Now, typename Sleep>
bool waitForBuildRoom(Status status, Now now, Sleep sleep) {
  using namespace retirement_fixture;
  using namespace build_room_fixture;
  struct Context {
    Status& status;
    Now& now;
    Sleep& sleep;
  } context{status, now, sleep};
  Calls calls{&context, [](void* raw) -> uint32_t { return static_cast<Context*>(raw)->status(); },
              [](void* raw) -> uint32_t { return static_cast<Context*>(raw)->now(); },
              [](void* raw, uint32_t ms) { static_cast<Context*>(raw)->sleep(ms); }};
  assert(!active && !modelDepth);
  active = &calls;
  auto leaves = leavesDefaults();
  auto runtime = modelRuntime();
  auto resources = resourcesDefaults();
  legacyRender.strip_release_idle = [](void* raw) -> uint8_t {
    physical();
    assert(raw == active);
    ++active->strips;
    return 0;
  };
  leaves.radio_before_build = []() -> uint8_t {
    physical();
    ++active->releases;
    return 1;
  };
  leaves.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    physical();
    assert(out && !peer);
    const uint32_t status = active->status(active->context);
    assert(status <= 7);
    *out = {};
    out->running = (status & 1) != 0;
    out->stopping = (status & 2) != 0;
    out->starting = (status & 4) != 0;
  };
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText text) {
    physical();
    assert(severity == 1);
    const std::string_view tag(reinterpret_cast<const char*>(origin.data), origin.len);
    const std::string_view message(reinterpret_cast<const char*>(text.data), text.len);
    assert(tag == "ERS" && message == "Bluetooth radio still holds the heap after 5000 ms; chapter build refused");
  };
  runtime.now_ms = []() -> uint32_t {
    physical();
    return active->now(active->context);
  };
  runtime.delay_ms = [](uint32_t ms) {
    physical();
    assert(ms == 50);
    active->sleep(active->context, ms);
  };
  const XpEpubTestHelpersSeed seed{&calls, nullptr, 0, 0, 0};
  void* owner = xp_epub_test_helpers_create_legacy_render(&seed, &leaves, &runtime, &resources, &legacyRender);
  assert(owner);
  // This existing test entry borrows the fixture's exclusive resource capability.
  const bool room = xp_epub_test_build_room(owner) != 0;
  assert(calls.strips == 1 && calls.releases == 1);
  xp_epub_test_helpers_destroy(owner);
  assert(!modelDepth);
  active = nullptr;
  return room;
}
inline bool buildRoomForRadio(bool running, bool stopping, bool starting) {
  uint32_t now = 0;
  return waitForBuildRoom([=] { return uint32_t(running) | (uint32_t(stopping) << 1) | (uint32_t(starting) << 2); },
                          [&] { return now; }, [&](uint32_t ms) { now += ms; });
}
