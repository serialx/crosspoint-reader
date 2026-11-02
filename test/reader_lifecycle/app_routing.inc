#include "base_session_fixture.h"
#include <algorithm>

namespace {
unsigned factoryCalls = 0, frequencyQueries = 0;
int expectedKind = -1, frequency = 15;
bool fastInitial = false;
std::string expectedPath;
void* (*sleepFactory)(const XpAppLaunch*);
XpUiReaderRenderServices rootRender{};

void bindRouting() {
  sleepFactory = legacy.create;
  legacy.create = [](const XpAppLaunch* launch) -> void* {
    assert(launch);
    if (launch->kind == XP_APP_SLEEP) return sleepFactory(launch);
    ++factoryCalls;
    assert(launch->kind == expectedKind && !f().componentAttempt);
    const auto path = launch->kind == XP_APP_FILES ? launch->value.file_path
                    : launch->kind == XP_APP_BITMAP ? launch->value.bitmap_path : launch->value.reader.path;
    assert(text(path) == expectedPath);
    if (launch->kind == XP_APP_READER) assert(bool(launch->value.reader.fast_initial) == fastInitial);
    auto* handle = f().acquire(Kind::Legacy, 0);
    handle->activityKind = launch->kind;
    handle->path = text(path);
    return handle;
  };
  leaves.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    auto& root = f().handle(raw, Kind::Root);
    assert(f().componentAttempt == 1 && root.generation == f().generation);
    assert(!f().renderDepth && !f().modelDepth);
    rootRender.context = raw;
    rootRender.read = [](void* context, uint8_t query) -> uint32_t {
      auto& root = f().handle(context, Kind::Root);
      assert(query == XP_UI_READER_RENDER_FREQUENCY && f().componentAttempt == 1);
      assert(!f().renderDepth && !f().modelDepth);
      ++frequencyQueries;
      f().physical("frequency", root.generation, root.id);
      return uint32_t(frequency);
    };
    rootRender.execute = [](void*, const XpUiReaderRenderOp*) -> uint32_t {
      assert(false && "construction must only query Frequency");
      return 0;
    };
    return &rootRender;
  };
}

uint8_t navigate(XpAppText path, bool fast) {
  XpAppEvent event{};
  event.kind = XP_APP_NAVIGATE;
  event.value.navigate.kind = XP_APP_READER;
  event.value.navigate.value.reader = {path, uint8_t(fast)};
  return xp_app_event(&event);
}

void closeRoute(bool owned) {
  drainToSleep();
  if (owned) assertExited();
  popFinalSleep();
  assert(f().handles.empty() && !f().renderDepth && !f().modelDepth);
  for (unsigned i = 0; i < kinds; ++i) assert(f().acquired[i] == f().released[i]);
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc >= 2);
  Fixture state;
  fixture = &state;
  bind();
  bindRouting();
  const std::string mode = argv[1];
  if (mode == "null-table") {
    XpAppCompatibility compatibility{};
    platform.epub = nullptr;
    assert(xp_app_init(&platform, &compatibility) == XP_APP_INVALID);
    assert(!compatibility.activity_facts && !factoryCalls && !state.componentAttempt);
    platform.epub = &leaves;
    initialize();
    assert(!xp_app_test_current_epub() && !xp_app_test_current_legacy());
    std::puts("PASS mandatory EPUB table rejects null without publishing App");
    return 0;
  }
  initialize();
  const size_t initialLive = xp_test_live_allocations();
  if (mode == "route") {
    assert(argc == 6);
    state.path = argv[2];
    expectedKind = std::atoi(argv[3]);
    fastInitial = std::atoi(argv[4]) != 0;
    frequency = std::atoi(argv[5]);
    expectedPath = state.path.empty() ? "/" : state.path;
    state.startAttempt();
    assert(navigate(wire(state.path), fastInitial) == XP_APP_ACCEPTED);
    const bool owned = expectedKind < 0;
    if (owned) {
      assertEntered();
      assert(!factoryCalls && state.componentAttempt == 4);
      assert(frequencyQueries == unsigned(fastInitial));
      XpEpubPublicationObservation observation{};
      xp_epub_test_publication_observe(xp_app_test_current_epub(), &observation);
      assert(observation.refresh_counter == (fastInitial ? std::max(frequency, 2) : 0));
      if (fastInitial) state.ordered(1, {"open:root", "frequency", "open:bookmarks", "open:overlay-popup",
                                       "open:failure-popup", "entry"});
    } else {
      assert(!xp_app_test_current_epub() && xp_app_test_current_legacy());
      assert(factoryCalls == 1 && !state.componentAttempt && !frequencyQueries && !state.entered);
    }
    closeRoute(owned);
  } else if (mode == "failure" || mode == "invalid-span") {
    state.startAttempt();
    assert(navigate(wire(state.path), false) == XP_APP_ACCEPTED);
    assertEntered();
    void* active = xp_app_test_current_epub();
    auto* extractor = state.extractor;
    const size_t activeLive = xp_test_live_allocations();
    if (mode == "invalid-span") {
      assert(navigate({nullptr, 1}, true) == XP_APP_REJECTED);
      assert(state.componentAttempt == 4 && !frequencyQueries);
    } else {
      assert(argc == 5);
      const std::string fault = argv[2];
      const unsigned index = unsigned(std::atoi(argv[3]));
      fastInitial = std::atoi(argv[4]) != 0;
      state.startAttempt(fault == "native" ? int(index) : -1);
      if (fault == "rust") xp_test_fail_allocation_after(index);
      assert(navigate(wire(state.path), fastInitial) == XP_APP_REJECTED);
      if (fault == "rust") assert(xp_test_allocation_attempts() == index + 1);
      xp_test_fail_allocation_after(SIZE_MAX);
      assert(state.componentAttempt == (fault == "native" ? index + 1 : 0));
      assert(frequencyQueries == unsigned(fastInitial && fault == "native" && index > 0));
      if (fault == "native") {
        std::vector<std::string> order;
        for (unsigned i = 0; i < index; ++i) order.push_back(std::string("open:") + names[i]);
        order.push_back(std::string("fail:") + names[index]);
        for (unsigned i = index; i > 0; --i) order.push_back(std::string("close:") + names[i - 1]);
        state.componentOrder(2, order);
      }
    }
    assert(xp_app_test_current_epub() == active && state.extractor == extractor && state.clippings == extractor);
    assert(!xp_app_test_pending_kind() && !factoryCalls && !state.exited && !state.clearedExtractor);
    assert(xp_test_live_allocations() == activeLive);
    closeRoute(true);
  } else {
    assert(false && "unknown routing case");
  }
  assert(xp_test_live_allocations() == initialLive);
  std::printf("PASS routing %s factories=%u frequency=%u components=%u\n", mode.c_str(), factoryCalls,
              frequencyQueries, state.componentAttempt);
}
