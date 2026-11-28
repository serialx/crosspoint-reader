namespace refresh_fixture {
struct HalDisplay {
  enum RefreshMode { FAST_REFRESH, FULL_REFRESH };
  static inline std::vector<RefreshMode> modes;
};
struct GfxRenderer {
  mutable bool promotedRefreshPending_ = false;
  mutable HalDisplay::RefreshMode promotedRefresh_ = HalDisplay::FAST_REFRESH;
  void promoteNextRefresh(HalDisplay::RefreshMode mode) {
    assert(f().renderDepth == 1);
    promotedRefreshPending_ = true;
    promotedRefresh_ = mode;
  }
  HalDisplay::RefreshMode applyPromotedRefresh(HalDisplay::RefreshMode) const;
  void display() {
    assert(f().renderDepth == 1);
    HalDisplay::modes.push_back(applyPromotedRefresh(HalDisplay::FAST_REFRESH));
  }
};
static GfxRenderer renderer;
static struct {
  bool present() const { return false; }
  void setBrightness(int) {}
  void setWarmth(int) {}
  void setOn(bool) {}
} Frontlight;
static struct {
  struct { bool enabled = false; } ble;
  int screenInverted = 0, orientation = 0, touchReaderControls = 0;
  int frontlightBrightness = 0, frontlightWarmth = 0, frontlightOn = 0;
  void saveToFile() { assert(false); }
} SETTINGS;
namespace bleturner { void switchOff() { assert(false); } }
namespace haptic_feedback { void touchAction() {} }
struct ActivityManager {
  void popActivity();
  void send(const XpAppEvent& event) { assert(xp_app_event(&event) == XP_APP_ACCEPTED); }
} activityManager;
struct Activity {
  void finish();
  void setResult(ActivityResult&&) { assert(false); }
};
struct UtilityActivity : Activity {
  XpUiUtilityState state{.kind = XP_UI_UTILITY_FRONTLIGHT};
  void applyUtilityEffect(const XpUiUtilityEffect&);
  void onExit() { assert(f().renderDepth == 1); }
};
struct FrontlightPanelActivity : UtilityActivity {
  GfxRenderer& renderer = refresh_fixture::renderer;
  bool refreshParent = false;
  void applyUtilityEffect(const XpUiUtilityEffect&);
  void onExit();
};
// PRODUCTION_REFRESH_METHODS
static FrontlightPanelActivity panel;

void bind() {
  static auto baseFacts = legacy.facts;
  static auto baseEnter = legacy.enter;
  static auto baseExit = legacy.exit;
  static auto baseDestroy = legacy.destroy;
  static auto baseRender = legacy.render;
  static auto baseResult = legacy.with_result;
  legacy.facts = [](const void* raw, uint8_t input, uint8_t flags) -> XpAppLegacyFacts {
    if (raw != &panel) return baseFacts(raw, input, flags);
    XpAppLegacyFacts facts{};
    facts.kind = XP_APP_FRONTLIGHT_PANEL;
    return facts;
  };
  legacy.enter = [](void* raw) {
    if (raw != &panel) baseEnter(raw);
  };
  legacy.exit = [](void* raw) {
    if (raw == &panel) panel.onExit();
    else baseExit(raw);
  };
  legacy.destroy = [](void* raw) {
    if (raw == &panel) assert(f().renderDepth == 1);
    else baseDestroy(raw);
  };
  legacy.render = [](void* raw, XpAppRenderToken* token) {
    if (raw == &panel) renderer.display();
    else baseRender(raw, token);
  };
  legacy.with_result = [](void* raw, XpAppResultSink sink, void* context) {
    if (raw == &panel) sink(context, nullptr);
    else baseResult(raw, sink, context);
  };
  static auto renderServices = resultRenderServices;
  renderServices.execute = [](void* context, const XpUiReaderRenderOp* op) -> uint32_t {
    if (op->kind == XP_UI_READER_RENDER_DISPLAY || op->kind == XP_UI_READER_RENDER_DISPLAY_ASYNC) renderer.display();
    return resultRenderServices.execute(context, op);
  };
  legacyBook.section.page.render_services = [](void*) -> const XpUiReaderRenderServices* {
    physical("render-services", 1);
    return &renderServices;
  };
}
void exercise() {
  XpAppEvent push{.kind = XP_APP_ADOPT_PUSH};
  push.value.adopt_push = &panel;
  assert(xp_app_event(&push) == XP_APP_ACCEPTED && xp_app_test_drain());
  assert(xp_app_test_current_legacy() == &panel && xp_app_test_parent_count() == 1);
  // The Control Center publishes Refresh as tile 1 under the TILE action.
  const XpUiInteraction refresh{.rect = {0, 0, 100, 100}, .action = 112, .value = 1, .inputs = 1};
  const XpUiScene scene{.interactions = &refresh, .interaction_count = 1};
  const XpUiUtilityFeedback feedback{};
  XpUiInput tap{};
  tap.touch_x = 10;
  tap.touch_y = 10;
  tap.touch_flags = XP_UI_TOUCH_RELEASED;
  const auto effect = xp_ui_utility_update(&panel.state, &tap, &scene, &feedback);
  assert(effect.navigation.kind == XP_UI_CANCEL && !effect.navigation.invalidate);
  panel.applyUtilityEffect(effect);
  assert(xp_app_test_pending_kind() == 2);
  // Let the already queued panel paint run before the navigation drain.
  HalDisplay::modes.clear();
  void* lock = runtime.render_lock();
  XpAppRenderToken token{lock};
  xp_app_render(&token);
  runtime.render_unlock(lock);
  assert((HalDisplay::modes == std::vector{HalDisplay::FAST_REFRESH}));
  assert(xp_app_test_drain() && xp_app_test_current_epub() == results.owner);
  assert(!xp_app_test_current_legacy() && !xp_app_test_parent_count());
  paintOwner();
  paintOwner();
  assert((HalDisplay::modes ==
          std::vector{HalDisplay::FAST_REFRESH, HalDisplay::FULL_REFRESH, HalDisplay::FAST_REFRESH}));
}
}  // namespace refresh_fixture
