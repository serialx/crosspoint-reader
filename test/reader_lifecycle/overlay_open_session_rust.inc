namespace overlay_open_adapter {
static EpubReaderActivity* active;
static unsigned modelDepth;
static void leaf() { assert(active && modelDepth == 0); }
static void unlocked() { leaf(); assert(lockDepth == 0); }
static void locked() { leaf(); assert(lockDepth == 1); }
static void context(void* value) { leaf(); assert(value == active); }
static void sync() { leaf(); assert(active->owner); active->observe(); }
static const XpAppResources resources = overlayUnexpectedResources();
static const XpAppRuntime runtime = [] {
  auto table = overlayUnexpectedRuntime();
  table.model_lock = [] { leaf(); ++modelDepth; };
  table.model_unlock = [] { assert(active && modelDepth == 1); --modelDepth; };
  table.render_lock = []() -> void* { unlocked(); return new RenderLock; };
  table.render_unlock = [](void* value) {
    locked(); assert(value); delete static_cast<RenderLock*>(value); unlocked();
  };
  return table;
}();
static XpUiReaderInputServices inputServices = {
  nullptr,
  &OverlayUnexpected<uint8_t (*)(void*, uint32_t, uint8_t, uint32_t)>::call,
  [](void* value, uint8_t query) -> uint32_t {
    context(value); unlocked(); assert(query == XP_UI_READER_INPUT_HAS_TOUCH);
    return active->mappedInput.hasTouch();
  },
  &OverlayUnexpected<uint8_t (*)(void*, int32_t*, int32_t*)>::call,
};
static XpUiReaderRenderServices renderServices = {
  nullptr,
  &OverlayUnexpected<uint32_t (*)(void*, uint8_t)>::call,
  [](void* value, const XpUiReaderRenderOp* op) -> uint32_t {
    context(value); locked(); assert(op && op->kind == XP_UI_READER_RENDER_STORE_BW);
    return active->renderer.storeBwBuffer();
  },
};
static const XpEpubLeaves leaves = [] {
  auto table = overlayUnexpectedLeaves();
  table.toolbar_open = []() -> void* {
    unlocked(); assert(!active->toolbarUi);
    active->toolbarUi = std::make_unique<ReaderToolbarUi>(active->renderer);
    return active->toolbarUi.get();
  };
  table.toolbar_begin = [](void* toolbar) {
    unlocked(); assert(toolbar == active->toolbarUi.get()); active->toolbarUi->begin();
  };
  table.toolbar_reset_navigation = [](void* toolbar, int32_t index, int32_t top) {
    leaf(); assert(toolbar == active->toolbarUi.get());
    active->toolbarUi->resetNavigation(index, top);
  };
  table.input_services = []() -> const XpUiReaderInputServices* {
    unlocked(); inputServices.context = active; return &inputServices;
  };
  table.render_services = [](void* root) -> const XpUiReaderRenderServices* {
    locked(); assert(root == active); renderServices.context = root; return &renderServices;
  };
  table.render_restore_bw = [](uint8_t resync) {
    locked(); assert(resync == 0); active->renderer.restoreBwBuffer(resync != 0);
  };
  return table;
}();
}  // namespace overlay_open_adapter

void EpubReaderActivity::initialize() {
  using namespace overlay_open_adapter;
  assert(!owner && !active && !modelDepth && !lockDepth);
  active = this;
  XpEpubTestOverlaySeed seed{};
  seed.native = this;
  seed.section = section ? reinterpret_cast<void*>(uintptr_t(1)) : nullptr;
  seed.toolbar = toolbarUi.get();
  seed.popup = &overlayPopup;
  seed.focus = focusedTool; seed.index = panelIndex;
  seed.overlay = static_cast<uint8_t>(overlay);
  seed.cursor = panelCursorShown; seed.hold = panelHoldJumped;
  seed.stored = overlayPageStored; seed.stale = pageBufferStale;
  seed.context = this;
  seed.home_reset = [](void* value) { context(value); unlocked(); active->mappedInput.resetHomeButtonInput(); };
  seed.current_toc = [](void* value) -> int32_t { context(value); locked(); return active->currentTocIndex(); };
  seed.build_more = [](void* value) { context(value); unlocked(); active->buildMoreActions(); };
  seed.paint = [](void* value) { context(value); locked(); sync(); active->renderOverlay(); };
  seed.push = [](void* value) { context(value); locked(); active->pushOverlayRefresh(); };
  seed.a4 = [](void*, uint8_t) { assert(false); };
  seed.xteink = [](void*, uint8_t) { assert(false); };
  seed.settle = [](void* value) { context(value); locked(); active->settleOverlayRefresh(); };
  seed.discard = [](void* value) { context(value); locked(); active->discardOverlayPage(); };
  seed.update = [](void* value) { context(value); unlocked(); sync(); active->requestUpdate(); };
  owner = xp_epub_test_overlay_create(&seed, &leaves, &runtime, &resources);
  assert(owner);
}
void EpubReaderActivity::observe() {
  using namespace overlay_open_adapter;
  leaf(); assert(active == this && owner);
  XpEpubTestOverlayObservation result{};
  xp_epub_test_overlay_observe(owner, &result);
  overlay = static_cast<Overlay>(result.overlay);
  focusedTool = result.focus; panelIndex = result.index;
  panelCursorShown = result.cursor; panelHoldJumped = result.hold;
  overlayPageStored = result.stored;
  assert(bool(result.toolbar) == bool(toolbarUi) && !result.failed);
}
void EpubReaderActivity::openOverlay(Overlay target) {
  initialize();
  xp_epub_test_overlay_open(owner, static_cast<uint8_t>(target));
  observe();
  overlay_open_adapter::unlocked();
}
EpubReaderActivity::~EpubReaderActivity() {
  using namespace overlay_open_adapter;
  unlocked(); assert(owner && active == this);
  xp_epub_test_overlay_destroy(owner);
  owner = nullptr; active = nullptr;
  // The fixture still owns toolbarUi; its original destructor emits the drop.
}
