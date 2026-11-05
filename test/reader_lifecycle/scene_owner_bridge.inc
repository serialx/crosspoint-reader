// App owns construction/destruction; every entry below calls an ordinary method.
#include "overlay_actions_session_fixture.h"
extern "C" {
void xp_epub_test_loop(void*);
void xp_epub_test_menu_open(void*);
void xp_epub_test_overlay_view_render(void*);
uint8_t xp_epub_test_entry_gate_render(void*);
struct XpEpubTestSceneSetup {
  uint8_t overlay, stored, hold_jumped, cursor;
  int32_t focused_tool, panel_index;
};
void xp_epub_test_scene_setup(void*, const XpEpubTestSceneSetup*);
}

struct ScenePhysical {
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  ReaderToolbarUi* toolbar = nullptr;
  void* owner = nullptr;
  unsigned notifications = 0, requested = 0, routes = 0, sampled = 0, swipes = 0, popupInputs = 0;
  unsigned restored = 0, stored = 0, redrawn = 0, pushed = 0, settled = 0, pageDrawn = 0;
  unsigned afterFailureRoutes = 0, afterFailureSamples = 0, dispatchInjections = 0, recoverInjections = 0;
  unsigned pagePasses = 0;
  bool popupActive = false, backup = false, refresh = false;
  bool rejectBeforeDispatch = false, recoverBeforeCancel = false, captureUnlock = false;
  bool loopActive = false, explicitBuild = false, bootstrapping = false;
  void* boundPage = nullptr;
};
static ScenePhysical scene;
static GfxRenderer* target;
static MappedInputManager* mapped;
static std::mutex sceneModelMutex;
static void sceneLeaf(int depth = -1) {
  assert(f().modelDepth == 0 && criticalDepth == 0);
  if (depth >= 0) assert(sceneRenderDepth == unsigned(depth));
}
static XpEpubTestOverlayObservation sceneObserve() {
  XpEpubTestOverlayObservation out{};
  assert(scene.owner);
  xp_epub_test_overlay_observe(scene.owner, &out);
  return out;
}
static void sceneBuild() {
  assert(scene.owner && !scene.explicitBuild && !sceneRenderDepth);
  scene.explicitBuild = true;
  { RenderLock access; xp_epub_test_overlay_view_render(scene.owner); }
  scene.explicitBuild = false;
}
static void sceneSetup(uint8_t mode, bool stored, bool hold) {
  const auto old = sceneObserve();
  XpEpubTestSceneSetup setup{mode, uint8_t(stored), uint8_t(hold), old.cursor, old.focus, old.index};
  xp_epub_test_scene_setup(scene.owner, &setup);
}

// ACTUAL_PATH_TEXT
// ACTUAL_SCOPED_CLEANUP
static const auto sceneNativePaint = /* ACTUAL_PAINT_LEAF */;
static const auto sceneNativeRoute = /* ACTUAL_ROUTE_LEAF */;
static const auto sceneNativeScroll = /* ACTUAL_SCROLL_LEAF */;
static const auto sceneNativeVisible = /* ACTUAL_VISIBLE_LEAF */;
static const auto sceneNativeReset = /* ACTUAL_RESET_LEAF */;
// ACTUAL_INPUT_SNAPSHOT

// Concrete physical callbacks are in a separate reusable include. They must not
// install any EpubSession test hook or construct a second owner.
#include "scene_physical_bindings.inc"

class EpubReaderActivity {
 public:
  Fixture appState;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  ReaderToolbarUi* toolbarUi = nullptr;  // Borrowed from the App-owned session.
  unsigned actions = 0, cancelled = 0;
  unsigned& notifications = scene.notifications;
  unsigned& requested = scene.requested;
  unsigned& routes = scene.routes;
  unsigned& sampled = scene.sampled;
  unsigned& swipes = scene.swipes;
  unsigned& popupInputs = scene.popupInputs;
  unsigned& restored = scene.restored;
  unsigned& stored = scene.stored;
  unsigned& redrawn = scene.redrawn;
  unsigned& pushed = scene.pushed;
  unsigned& settled = scene.settled;
  unsigned& pageDrawn = scene.pageDrawn;
  bool& popupActive = scene.popupActive;
  bool& recoverBeforeCancel = scene.recoverBeforeCancel;
  bool& rejectBeforeDispatch = scene.rejectBeforeDispatch;
  bool panelHoldJumped = false;
  unsigned lockDepth = 0;
  uint32_t fields[8]{};

  EpubReaderActivity(GfxRenderer& r, MappedInputManager& input) : renderer(r), mappedInput(input) {
    fixture = &appState;
    scene.renderer = target = &r;
    scene.input = mapped = &input;
    scene.bootstrapping = true;
    bind();
    bindScenePhysical();
    initialize();
    appState.startAttempt();
    assert(install() == 1 && xp_app_test_current_epub());
    scene.owner = xp_app_test_current_epub();
    // Real entry and full Page render load the Section; no resource pointer seed.
    { RenderLock access; assert(xp_epub_test_entry_gate_render(scene.owner)); }
    if (!sceneSectionLive() || scene.pageDrawn != 1 || scene.boundPage) {
      std::fprintf(stderr, "initial render section=%u pageDrawn=%u bound=%u\n", unsigned(sceneSectionLive()),
                   scene.pageDrawn, unsigned(scene.boundPage != nullptr));
      appState.dump(); std::fflush(stdout);
    }
    assert(sceneSectionLive() && scene.pageDrawn == 1 && scene.pagePasses == 2 && !scene.boundPage);
    // Unlike open_overlay, this installs an unpainted toolbar for prepaint/first.
    xp_epub_test_menu_open(scene.owner);
    toolbarUi = scene.toolbar;
    assert(toolbarUi && !toolbarUi->ready && sceneObserve().overlay == 1);
    xp_app_test_drain();
    scene.bootstrapping = false;
    resetObservedEffects();
    observe();
  }
  ~EpubReaderActivity() {
    assert(!sceneRenderDepth && !appState.modelDepth);
    failure = 0; allocationFails = false; renderer.paintOk = true;
    scene.captureUnlock = scene.recoverBeforeCancel = scene.rejectBeforeDispatch = false;
    drainToSleep();
    assert(!xp_app_test_current_epub() && !scene.toolbar && !sceneSectionLive() && !scene.boundPage);
    scene.owner = nullptr;
    popFinalSleep();
    assert(appState.handles.empty());
    fixture = nullptr;
  }
  void resetObservedEffects() {
    scene.notifications = scene.requested = scene.routes = scene.sampled = scene.swipes = scene.popupInputs = 0;
    scene.restored = scene.stored = scene.redrawn = scene.pushed = scene.settled = scene.pageDrawn = scene.pagePasses = 0;
    scene.afterFailureRoutes = scene.afterFailureSamples = scene.dispatchInjections = scene.recoverInjections = 0;
  }
  void observe() {
    const auto out = sceneObserve();
    panelHoldJumped = out.hold;
    fields[0] = out.overlay;
    fields[7] = out.stored;  // Original trace slot for the stored-page observation.
    lockDepth = sceneRenderDepth;
  }
  void loop() {
    const auto before = sceneObserve();
    scene.loopActive = true;
    xp_epub_test_loop(scene.owner);
    scene.loopActive = false;
    xp_app_test_drain();
    assert(scene.owner == xp_app_test_current_epub());
    const auto after = sceneObserve();
    // Read-only equivalence of original fake OPEN/CLOSE action counters.
    if (before.overlay == 1 && after.overlay != 1) ++actions;
    if (before.overlay == 1 && after.overlay == 0) ++cancelled;
    observe();
  }
  void build() {
    const unsigned previous = notifications;
    sceneBuild(); observe();
    if (notifications != previous) {
      assert(notifications == previous + 1 && rejected() && !panelHoldJumped && !toolbarUi->ready);
      assert(toolbarUi->contact.active == -1 && toolbarUi->contact.last_drag_x == -1 && !toolbarUi->contact.contact_held);
    }
  }
  bool rejected() const { return sceneObserve().failed; }
  void render(RenderLock&&) {
    assert(sceneRenderDepth == 1);
    assert(xp_epub_test_entry_gate_render(scene.owner));
    observe();
  }
  void primeMixedRefresh() {
    // Real successful push makes the later Cleanup observable without a hook.
    xp_epub_test_overlay_open(scene.owner, 1);
    assert(scene.refresh);
  }
  void seedMixed(const std::string& variant) {
    uint8_t mode = variant == "contents" ? 2 : variant == "text" || variant == "popup" ? 3 :
                   variant == "more" ? 4 : 1;
    { RenderLock access; sceneStoreBackup(); }
    scene.popupActive = variant == "popup";
    sceneSetup(mode, true, sceneObserve().hold);
    // Original counters started at the cancellation boundary, not earlier setup.
    scene.restored = scene.stored = scene.redrawn = scene.pushed = scene.settled = 0;
    observe();
  }
  void seedHeldPanel() {
    const auto old = sceneObserve();
    sceneSetup(old.overlay, old.stored, true);
    observe();
  }
};
