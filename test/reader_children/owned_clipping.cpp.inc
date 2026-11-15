static bool clippingOomExpected, clippingDetailOomExpected, clippingReadFailure;
static unsigned clippingDetailOomLogs, clippingDetailFills;
static unsigned clippingOomLogs;
static bool clippingPopup, clippingLong, clippingDetailFailure, clippingStackFailure;
static int clippingChoice = -1;
static void (*clippingChoose)(void*, int32_t);
static void* clippingChoiceContext;
static unsigned clippingLoads, clippingDetailCreates, clippingDetailDrops;
extern "C" void* xp_app_test_current_definition();
extern "C" uint8_t xp_app_test_definition_result(uint8_t);
struct ClippingDetailNative { bool entered = false; };
static_assert(sizeof(ClippingDetailNative) <= sizeof(XpDefinitionPayloadStorage));
static void* clippingDetailHandle;
static std::string clippingDetailText, clippingDetailTitle;
namespace {
static bool observeOwnedClippingLog(uint8_t level, XpAppText origin, XpAppText message) {
  if (clippingDetailOomExpected) {
    assert(level == 1 && text(origin) == "CLIP" &&
           text(message) == "Failed to allocate clipping detail activity");
    ++clippingDetailOomLogs;
    return true;
  }
  if (!clippingOomExpected) return false;
  assert(level == 1 && origin.len == 4 && !std::memcmp(origin.data, "CLIP", 4));
  assert(message.len == 18 && !std::memcmp(message.data, "OOM: clipping list", 18));
  ++clippingOomLogs;
  return true;
}
}  // namespace
static void bindOwnedClippings() {
  static auto priorUpdate = runtime.notify_render;
  runtime.notify_render = [] { assert(!clippingOomExpected); priorUpdate(); };

  static auto open = leaves.popup_open;
  leaves.popup_open = []() -> void* {
    if (f().componentAttempt < 3) return open();
    return f().acquire(Kind::OverlayPopup, 0);
  };
  leaves.popup_show = [](void*, XpAppText, XpAppText, uint32_t count, int32_t selected,
      uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labels,
      void (*choice)(void*, int32_t), void* context) {
    assert(f().renderDepth == 1 && count == 2 && selected == 0 && !clippingPopup);
    for (uint32_t i = 0; i < count; ++i)
      assert(label(labels, i, [](void*, XpAppText value) -> uint8_t { assert(value.len); return 1; }, nullptr));
    clippingPopup = true; clippingChoose = choice; clippingChoiceContext = context;
  };
  leaves.popup_active = [](void*) -> uint8_t { return clippingPopup; };
  leaves.popup_input = [](void*, XpEpubPopupInput* out) {
    *out = {}; out->consumed = clippingPopup;
    if (clippingPopup && clippingChoice >= 0) {
      clippingChoose(clippingChoiceContext, clippingChoice); clippingChoice = -1;
      clippingPopup = false; out->changed = 1;
    }
  };
  leaves.popup_process_render = [](void*) { assert(f().renderDepth == 1 && clippingPopup); };
  resources.popup_draw = [](XpAppText text) -> XpUiRect {
    assert(f().renderDepth == 1 && text.len); ++clippingLoads; return {};
  };
  leaves.child_list_help = [](XpUiRect, XpAppText text) { assert(f().renderDepth == 1 && text.len); };
  leaves.child_list_icon = [](uint8_t kind, XpUiRect, XpUiRect, uint8_t paint) { assert(kind == 0 && f().renderDepth == 1 && paint <= 1); };
  leaves.input_screen_point = [](uint8_t, int32_t*, int32_t*) -> uint8_t { return 0; };
  leaves.input_services = []() -> const XpUiReaderInputServices* {
    static const XpUiReaderInputServices service{
      nullptr,
      [](void*, uint32_t button, uint8_t, uint32_t duration) -> uint8_t {
        assert(button == XP_UI_CONFIRM && duration == 700); return clippingLong;
      },
      [](void*, uint8_t) -> uint32_t { return 1; },
      [](void*, int32_t*, int32_t*) -> uint8_t { return 0; }
    };
    return &service;
  };
  leaves.child_definition_payload_plain = [](XpDefinitionPayloadStorage* storage, XpAppText title, uint32_t length,
      XpEpubDefinitionFill fill, void* context) -> uint8_t {
    assert(!f().renderDepth && !f().modelDepth);
    clippingDetailTitle = text(title);
    clippingDetailText.assign(length, '\0');
    ++clippingDetailFills;
    if (clippingReadFailure) fake::failRead = 0;
    const bool filled = fill(context, reinterpret_cast<uint8_t*>(clippingDetailText.data()), length);
    fake::failRead = -1;
    assert(filled == !clippingReadFailure);
    if (!filled) return 0;
    if (clippingDetailFailure) return 0;
    new (storage) ClippingDetailNative;
    ++clippingDetailCreates;
    clippingDetailHandle = storage;
    assert(results.children.insert(storage).second);
    if (clippingStackFailure) xp_test_fail_allocation_after(0);
    return 1;
  };
  leaves.child_definition_payload_drop = [](XpDefinitionPayloadStorage* storage) {
    assert(!f().modelDepth && clippingDetailHandle == storage);
    assert(f().renderDepth == (reinterpret_cast<ClippingDetailNative*>(storage)->entered ? 1 : 0));
    assert(results.children.erase(storage) == 1);
    auto* owner = reinterpret_cast<ClippingDetailNative*>(storage);
    std::destroy_at(owner);
    clippingDetailHandle = nullptr;
    ++clippingDetailDrops;
  };
  static auto destroy = legacy.destroy;
  legacy.destroy = [](void* raw) { assert(raw != clippingDetailHandle); destroy(raw); };
  leaves.child_definition_payload_prepare = [](XpDefinitionPayloadStorage* storage, uint8_t slot,
      uint8_t html, uint8_t plain, XpUiRect, XpDefinitionPayloadView* out) -> uint8_t {
    assert(!f().renderDepth && slot == 0 && html == 0 && plain == 0);
    reinterpret_cast<ClippingDetailNative*>(storage)->entered = true;
    *out = {}; out->pages = 1; return 1;
  };
  leaves.child_definition_layout = [](XpUiDefinitionLayout* out, int32_t x, int32_t y) -> uint8_t {
    assert(x == -1 && y == -1); *out = {}; return 0;
  };
  static auto releaseFonts = leaves.fonts_release_caches;
  leaves.fonts_release_caches = [] { assert(f().renderDepth == 1); releaseFonts(); };

}
static void nextClipping() {
  childInput = {}; childInput.input.now_ms = 100; childInput.input.pressed = XP_UI_NAV_NEXT;
  xp_app_test_activity_loop();
}
static int runOwnedClippings(const std::string& mode, bool cancelled) {
  xp_epub_test_clipping_observe(results.owner, nullptr, nullptr);
  xp_epub_test_clipping_results(results.owner, nullptr, nullptr);
  if (mode == "clippings-nav" || mode == "clippings-detail" || mode == "clippings-detail-oom") {
    XpClippingRecord record{}; record.spine = 2; record.page_count = 10;
    record.start_offset = 100; record.end_offset = 104;
    const std::string selection(4096, 's');
    const unsigned count = mode == "clippings-nav" ? 17 : 1;
    for (unsigned i = 0; i < count; ++i) {
      record.id[0] = char('b' + i);
      assert(xp_epub_test_clipping_insert(results.owner, &record, wire("Chapter " + std::to_string(i)), {wire(selection), &selection}) == 0);
    }
  }
  if (mode == "clippings-oom") {
    const auto live = xp_test_live_allocations();
    clippingOomExpected = true;
    xp_test_fail_allocation_after(0);
    xp_epub_test_menu_confirm(results.owner, 11);
    xp_test_fail_allocation_after(SIZE_MAX);
    clippingOomExpected = false;
    assert(clippingOomLogs == 1);
    assert(!xp_app_test_current_child(nullptr) && !xp_app_test_pending_kind());
    assert(xp_app_test_current_epub() == results.owner && xp_test_live_allocations() == live);
  } else {
    xp_epub_test_menu_confirm(results.owner, 11);
    assert(xp_app_test_pending_kind() == 1 && xp_app_test_drain());
    assert(childKind() == 7); auto* list = xp_app_test_current_child(nullptr);
    paintChild(); assert(clippingLoads == 1);
    if (mode == "clippings-nav") {
      for (unsigned i = 0; i < 10; ++i) { nextClipping(); paintChild(); }
      assert(xp_clippings_p18_count() == 18 && clippingLoads == 1);
    }
    if (mode == "clippings-delete") {
      clippingLong = true; inputChild(0); clippingLong = false;
      assert(clippingPopup);
      clippingChoice = cancelled ? 0 : 1;
      if (cancelled) { inputChild(0); assert(xp_clippings_p18_count() == 1); returnChild(XP_UI_BACK); }
      else { returnChild(0); assert(xp_clippings_p18_count() == 0); }
    } else if (mode == "clippings-passage") {
      inputChild(XP_UI_CONFIRM); assert(clippingPopup); clippingChoice = 1; returnChild(0);
    } else if (mode == "clippings-detail" || mode == "clippings-detail-oom" || mode == "clippings-nav") {
      if (mode != "clippings-nav") { nextClipping(); paintChild(); }
      if (mode == "clippings-detail") {
        inputChild(XP_UI_CONFIRM); assert(clippingPopup); clippingChoice = 0;
        const auto live = xp_test_live_allocations();
        clippingDetailOomExpected = true;
        xp_test_fail_allocation_after(0); inputChild(0); xp_test_fail_allocation_after(SIZE_MAX);
        clippingDetailOomExpected = false;
        assert(clippingDetailOomLogs == 1 && clippingDetailFills == 0);
        assert(xp_app_test_current_child(nullptr) == list && !xp_app_test_pending_kind());
        assert(xp_test_live_allocations() == live);
        paintChild();
        clippingReadFailure = true;
        inputChild(XP_UI_CONFIRM); assert(clippingPopup); clippingChoice = 0; inputChild(0);
        clippingReadFailure = false;
        assert(clippingDetailFills == 1 && clippingDetailOomLogs == 1 && !clippingDetailCreates);
        assert(xp_app_test_current_child(nullptr) == list && !xp_app_test_pending_kind());
        assert(xp_test_live_allocations() == live);
        paintChild();
      }
      clippingDetailFailure = mode == "clippings-detail-oom";
      inputChild(XP_UI_CONFIRM); assert(clippingPopup); clippingChoice = 0; inputChild(0);
      assert(clippingDetailText == std::string(4096, 's'));
      assert(clippingDetailTitle == (mode == "clippings-nav" ? "Chapter 9" : "Chapter 0"));
      if (!clippingDetailFailure) {
        assert(xp_app_test_current_definition() && !xp_app_test_current_child(nullptr));
        XpAppEvent event{}; event.kind = XP_APP_POP;
        assert(xp_app_test_definition_result(cancelled));
        assert(xp_app_event(&event) == XP_APP_ACCEPTED && xp_app_test_drain());
        assert(xp_app_test_current_child(nullptr) == list && xp_app_test_parent_count() == 1);
        assert(!xp_app_test_current_definition() && clippingDetailCreates == 1 && clippingDetailDrops == 1);
        if (mode == "clippings-detail") {
          for (unsigned visit = 2; visit <= 3; ++visit) {
            paintChild();
            inputChild(XP_UI_CONFIRM); assert(clippingPopup); clippingChoice = 0; inputChild(0);
            assert(xp_app_test_current_definition() && clippingDetailCreates == visit);
            assert(xp_app_test_definition_result(cancelled));
            assert(xp_app_event(&event) == XP_APP_ACCEPTED && xp_app_test_drain());
            assert(xp_app_test_current_child(nullptr) == list && clippingDetailDrops == visit);
          }
        }
      } else assert(xp_app_test_current_child(nullptr) == list && !clippingDetailCreates);
      paintChild(); assert(clippingLoads == 1);
      if (mode == "clippings-detail") {
        const bool debug = childDebugEnabled;
        childDebugEnabled = false;
        for (unsigned depth = 2; depth <= 4; ++depth) {
          xp_epub_test_menu_confirm(results.owner, 11);
          assert(xp_app_test_drain() && xp_app_test_parent_count() == depth);
          paintChild();
        }
        auto* retained = xp_app_test_current_child(nullptr);
        const auto creates = clippingDetailCreates, drops = clippingDetailDrops;
        const auto live = xp_test_live_allocations();
        clippingStackFailure = true; results.allocatingStack = true;
        inputChild(XP_UI_CONFIRM); assert(clippingPopup); clippingChoice = 0; inputChild(0);
        assert(xp_test_allocation_attempts() == 1);
        xp_test_fail_allocation_after(SIZE_MAX); clippingStackFailure = false; results.allocatingStack = false;
        assert(xp_app_test_current_child(nullptr) == retained && xp_app_test_parent_count() == 4);
        assert(!xp_app_test_pending_kind() && clippingDetailCreates == creates + 1 && clippingDetailDrops == drops + 1);
        assert(xp_test_live_allocations() == live);
        paintChild();
        XpAppEvent pop{}; pop.kind = XP_APP_POP;
        for (unsigned depth = 4; depth > 1; --depth)
          assert(xp_app_event(&pop) == XP_APP_ACCEPTED && xp_app_test_drain());
        assert(xp_app_test_current_child(nullptr) == list && xp_app_test_parent_count() == 1);
        childDebugEnabled = debug;
      }
      returnChild(XP_UI_BACK);
    } else returnChild(XP_UI_BACK);
    assert(xp_app_test_current_epub() == results.owner && !xp_epub_test_has_child(results.owner));
  }
  closeToSleep();
  assert(readerFiles.empty() && readerBooks.empty() && readerBuffers.empty());
  assert(readerSectionOpens == readerSectionCloses && f().handles.empty());
  assert(results.children.empty() && results.born == results.dead);
  assert(clipping_fixture::readerLiveAllocations() == (mode == "clippings-oom" ? 0u : 1u));
  assert(activeChildMarker.empty() && !f().renderDepth && !f().modelDepth);
  std::printf("owned App %s cancelled=%u: real store, balanced list/popup/detail and parent publication\n", mode.c_str(), cancelled);
  return 0;
}
