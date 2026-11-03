namespace close_overlay_adapter {
static EpubReaderActivity* active;
static unsigned modelDepth, lockDepth;
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
  table.render_lock = []() -> void* {
    unlocked(); sync(); ++lockDepth; return new RenderLock;
  };
  table.render_unlock = [](void* value) {
    locked(); assert(value); sync(); delete static_cast<RenderLock*>(value); --lockDepth; unlocked();
  };
  return table;
}();
static XpUiReaderRenderServices renderServices = {
  nullptr,
  &OverlayUnexpected<uint32_t (*)(void*, uint8_t)>::call,
  [](void* value, const XpUiReaderRenderOp* op) -> uint32_t {
    context(value); locked(); sync();
    assert(op && op->kind == XP_UI_READER_RENDER_DISPLAY && op->mode == XP_UI_FAST);
    active->renderer.displayBuffer(HalDisplay::FAST_REFRESH); return 0;
  },
};
static const LegacyEpubLeaves leaves = [] {
  auto table = overlayLegacyLeaves();
  table.physical.popup_dismiss = [](void* popup) {
    unlocked(); sync(); assert(popup == &active->overlayPopup); active->overlayPopup.dismiss();
  };
  table.physical.toolbar_close = [](void* toolbar) {
    locked(); sync(); assert(toolbar && toolbar == active->toolbarUi.get()); active->toolbarUi.reset();
  };
  table.physical.render_geometry = [](XpEpubGeometry* out) {
    unlocked(); assert(out); *out = {}; out->a4 = isA4; out->xteink_panel = isXteink;
  };
  table.physical.render_services = [](void* root) -> const XpUiReaderRenderServices* {
    locked(); assert(root == active); renderServices.context = root; return &renderServices;
  };
  table.physical.render_restore_bw = [](uint8_t resync) {
    locked(); sync(); assert(resync == 0); active->renderer.restoreBwBuffer(resync != 0);
  };
  table.physical.book_set_extractor = [](void* handle) {
    if (handle) leaves.book.book_install_extractor(handle);
    else leaves.book.book_clear_extractor();
  };
  return table;
}();
}  // namespace close_overlay_adapter

void EpubReaderActivity::initialize() {
  using namespace close_overlay_adapter;
  assert(!owner && !active && !modelDepth && !lockDepth);
  active = this;
  XpEpubTestOverlaySeed seed{};
  seed.native = this; seed.toolbar = toolbarUi.get(); seed.popup = &overlayPopup;
  seed.section = section ? reinterpret_cast<void*>(uintptr_t(1)) : nullptr;
  seed.focus = focusedTool; seed.index = panelIndex; seed.counter = pagesUntilFullRefresh;
  seed.overlay = static_cast<uint8_t>(overlay);
  seed.cursor = panelCursorShown; seed.hold = panelHoldJumped;
  seed.stored = overlayPageStored; seed.stale = pageBufferStale;
  seed.context = this;
  seed.home_reset = [](void* value) { context(value); unlocked(); sync(); active->mappedInput.resetHomeButtonInput(); };
  seed.current_toc = [](void* value) -> int32_t { context(value); locked(); return active->currentTocIndex(); };
  seed.build_more = [](void* value) { context(value); active->buildMoreActions(); };
  seed.paint = [](void* value) { context(value); locked(); sync(); active->renderOverlay(); };
  seed.push = [](void* value) { context(value); locked(); sync(); active->pushOverlayRefresh(); };
  seed.a4 = [](void* value, uint8_t actual) {
    context(value); unlocked(); sync(); assert(actual == uint8_t(isA4));
    const bool observed = BoardConfig::isEegoA4(); assert(observed == bool(actual));
  };
  seed.xteink = [](void* value, uint8_t actual) {
    context(value); unlocked(); sync(); assert(actual == uint8_t(isXteink));
    const bool observed = xteinkClassPanel(); assert(observed == bool(actual));
  };
  seed.settle = [](void* value) { context(value); locked(); sync(); active->settleOverlayRefresh(); };
  seed.discard = [](void* value) { context(value); locked(); sync(); active->discardOverlayPage(); };
  seed.update = [](void* value) { context(value); unlocked(); sync(); active->requestUpdate(); };
  owner = xp_epub_test_overlay_create_legacy_book(&seed,&leaves.physical, &runtime, &resources, &leaves.book);
  assert(owner);
}
void EpubReaderActivity::observe() {
  using namespace close_overlay_adapter;
  leaf(); assert(active == this && owner);
  XpEpubTestOverlayObservation result{};
  xp_epub_test_overlay_observe(owner, &result);
  overlay = static_cast<Overlay>(result.overlay);
  focusedTool = result.focus; panelIndex = result.index;
  panelCursorShown = result.cursor; panelHoldJumped = result.hold;
  overlayPageStored = result.stored; pagesUntilFullRefresh = result.counter;
  assert(!result.failed);
  // toolbar_close observes after Rust takes its slot and before the native reset.
}
void EpubReaderActivity::closeOverlayToPage() {
  initialize();
  xp_epub_test_overlay_close(owner);
  observe();
  close_overlay_adapter::unlocked();
  assert(!toolbarUi);
}
EpubReaderActivity::~EpubReaderActivity() {
  using namespace close_overlay_adapter;
  unlocked(); assert(owner && active == this);
  xp_epub_test_overlay_destroy(owner);
  owner = nullptr; active = nullptr;
}
