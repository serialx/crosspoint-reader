// Draft physical SDK/storage facts. No owner policy callbacks are installed.
// Unexpected defaults remain callable; an unbound physical site is a harness error.
struct SceneSection { int spine; };
struct ScenePage { int page; };
static SceneSection* sceneSection;
static bool sceneSectionLive() { return sceneSection != nullptr; }
static void sceneStoreBackup() { sceneLeaf(1); scene.backup = true; ++scene.stored; }
static const XpUiReaderRenderServices sceneRenderServices{
  nullptr,
  [](void*, uint8_t query) -> uint32_t {
    sceneLeaf(1);
    switch (query) {
      case XP_UI_READER_RENDER_NOW: return 100;
      case XP_UI_READER_RENDER_FREQUENCY: return 3;
      case XP_UI_READER_RENDER_FREE_HEAP:
      case XP_UI_READER_RENDER_MAX_BLOCK: return 100000;
      case XP_UI_READER_RENDER_HEIGHT: return 792;
      case XP_UI_READER_RENDER_WIDTH_BYTES: return 66;
      case XP_UI_READER_RENDER_HAS_IMAGES:
      case XP_UI_READER_RENDER_IMAGES_NEED_DECODE:
      case XP_UI_READER_RENDER_TEXT_AA:
      case XP_UI_READER_RENDER_IS_A4:
      case XP_UI_READER_RENDER_IS_X3:
      case XP_UI_READER_RENDER_IS_UC8279:
      case XP_UI_READER_RENDER_ABSOLUTE_SUPPORTED:
      case XP_UI_READER_RENDER_OVERLAY_CAPS:
      case XP_UI_READER_RENDER_ABSOLUTE_CAPS:
      case XP_UI_READER_RENDER_STRIP_BOARD:
      case XP_UI_READER_RENDER_ELEMENTS: return 0;
      case XP_UI_READER_RENDER_IS_BW: return 1;
      default: assert(false && "unresolved physical render query"); return 0;
    }
  },
  [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
    sceneLeaf(1); assert(op);
    switch (op->kind) {
      case XP_UI_READER_RENDER_PAGE: assert(scene.boundPage); ++scene.pagePasses; return 1;
      case XP_UI_READER_RENDER_STORE_BW: sceneStoreBackup(); return 1;
      case XP_UI_READER_RENDER_CLEANUP: scene.refresh = false; ++scene.settled; return 1;
      case XP_UI_READER_RENDER_DISPLAY_ASYNC: scene.refresh = true; ++scene.pushed; return 1;
      case XP_UI_READER_RENDER_DISPLAY: return 1;
      case XP_UI_READER_RENDER_CLEAR:
      case XP_UI_READER_RENDER_IMAGE_RESET:
      case XP_UI_READER_RENDER_PREWARM_BEGIN:
      case XP_UI_READER_RENDER_PREWARM_END:
      case XP_UI_READER_RENDER_MODE:
      case XP_UI_READER_RENDER_MARK:
      case XP_UI_READER_RENDER_LOG:
      case XP_UI_READER_RENDER_RELEASE_STRIPS: return 0;
      default: assert(false && "unresolved physical render operation"); return 0;
    }
  }
};

static void bindScenePhysical() {
  // f().renderDepth is kept in sync for the borrowed scaffold's physical leaves.
  // Actual exclusion belongs to the shared RenderLock mutex in Activity.h.
  runtime.model_lock = [] { sceneModelMutex.lock(); assert(!f().modelDepth); f().trace("model-lock"); f().modelDepth = 1; };
  runtime.model_unlock = [] {
    assert(f().modelDepth == 1); f().trace("model-unlock"); f().modelDepth = 0; sceneModelMutex.unlock();
    if (scene.captureUnlock) {
      scene.captureUnlock = false; scene.recoverBeforeCancel = false;
      ++scene.recoverInjections;
      failure = 0; allocationFails = false; target->paintOk = true;
      sceneBuild();  // Captured failed=true is already a local Rust scalar.
    }
  };
  runtime.render_lock = []() -> void* { sceneLeaf(0); f().physical("render-lock"); return new RenderLock; };
  runtime.render_try_lock = []() -> void* {
    sceneLeaf(0);
    if (!renderMutex.try_lock()) return nullptr;
    return new RenderLock(SceneAdoptRenderLock{});
  };
  runtime.render_unlock = [](void* raw) {
    sceneLeaf(1); f().physical("render-unlock"); delete static_cast<RenderLock*>(raw);
  };
  runtime.now_ms = []() -> uint32_t { sceneLeaf(); return 100; };
  runtime.notify_render = [] { sceneLeaf(); ++scene.requested; };
  runtime.touch_feedback = [](uint8_t) { sceneLeaf(); };
  leaves.render_busy = []() -> uint8_t { sceneLeaf(); return 0; };
  leaves.input_snapshot = [](XpUiInput* out) {
    sceneLeaf(0); assert(out); *out = rustInputSnapshot(*mapped);
    if (scene.recoverBeforeCancel) { assert(!scene.captureUnlock); scene.captureUnlock = true; }
  };
  leaves.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    sceneLeaf(); assert(resolve <= 1 && out); *out = {};
    out->embedded_style = 1; out->menu_style = 1; out->font = resolve ? 1 : 0;
    out->font_size = 14; out->font_family = {reinterpret_cast<const uint8_t*>("fixture"), 7};
    // Page touch is disabled; the real toolbar still sees raw touch snapshots.
  };
  leaves.render_geometry = [](XpEpubGeometry* out) {
    sceneLeaf(); *out = {}; out->width = 528; out->height = 792;
    out->has_framebuffer = 1; out->async_refresh = 1; out->title_mode = 2; out->ui_font_12 = 1;
  };
  leaves.render_margins = [](int32_t* out) { sceneLeaf(1); for (int i = 0; i < 4; ++i) out[i] = 0; };
  leaves.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    sceneLeaf(1); *out = {}; out->width = width; out->height = height; out->font = 1;
    out->compression = 1; out->word_spacing_percent = 100;
  };
  legacyBook.section.page.render_services = [](void*) -> const XpUiReaderRenderServices* { sceneLeaf(1); return &sceneRenderServices; };
  legacyBook.section.page.render_bind_page = [](void*, void* page, int32_t, int32_t, int32_t, int32_t, XpEpubLegacyHighlight, void*) {
    sceneLeaf(1); assert(page && !scene.boundPage); scene.boundPage = page;
  };
  legacyBook.section.page.render_finish_page = [](void*) { sceneLeaf(1); assert(scene.boundPage); scene.boundPage = nullptr; ++scene.pageDrawn; };
  leaves.render_restore_bw = [](uint8_t resync) { sceneLeaf(1); assert(!resync && scene.backup); ++scene.restored; };
  leaves.render_discard_bw = [] { sceneLeaf(1); assert(scene.backup); scene.backup = false; };
  legacyBook.book_spine_count = [](void*) -> int32_t {
    sceneLeaf();
    if (scene.rejectBeforeDispatch && scene.loopActive && !scene.explicitBuild) {
      sceneLeaf(0); scene.rejectBeforeDispatch = false; ++scene.dispatchInjections; sceneBuild();
      assert(sceneObserve().failed && scene.notifications == 1);
    }
    return 7;
  };
  legacyBook.book_size = [](void*) -> uint32_t { sceneLeaf(); return 1000; };
  legacyBook.book_progress = [](void*, int32_t, float) -> float { sceneLeaf(); return 0.1f; };
  legacyBook.book_toc_count = [](void*) -> int32_t { sceneLeaf(); return 5; };
  legacyBook.book_toc_for_spine = [](void*, int32_t) -> int32_t { sceneLeaf(); return 0; };
  legacyBook.book_spine = [](void*, int32_t, XpEpubSpineSink sink, void* context) -> uint8_t {
    sceneLeaf(1); const XpEpubSpine value{{reinterpret_cast<const uint8_t*>("chapter.xhtml"), 13}, 100};
    return sink(context, &value);
  };
  legacyBook.book_toc = [](void*, int32_t, XpEpubTocSink sink, void* context) -> uint8_t {
    sceneLeaf(); const XpEpubToc value{{reinterpret_cast<const uint8_t*>("Chapter"), 7}, {nullptr, 0}, 1, 0};
    return sink(context, &value);
  };
  legacyBook.book_language = [](void*, XpAppTextSink sink, void* context) -> uint8_t { sceneLeaf(); return deliver(sink, context, "en"); };
  legacyBook.book_title = [](void*, XpAppTextSink sink, void* context) -> uint8_t { sceneLeaf(); return deliver(sink, context, "Fixture"); };
  legacyBook.section.section_open = [](void*, int32_t spine) -> void* { sceneLeaf(1); assert(!sceneSection); return sceneSection = new SceneSection{spine}; };
  legacyBook.section.section_close = [](void* raw) { sceneLeaf(1); assert(raw == sceneSection && !scene.boundPage); delete sceneSection; sceneSection = nullptr; };
  legacyBook.section.section_page_count = [](void* raw) -> uint16_t { sceneLeaf(1); assert(raw == sceneSection); return 10; };
  legacyBook.section.section_estimated_pages = [](void* raw) -> int32_t { sceneLeaf(1); assert(raw == sceneSection); return 10; };
  legacyBook.section.section_building = [](void* raw) -> uint8_t { sceneLeaf(1); assert(raw == sceneSection); return 0; };
  legacyBook.section.section_complete = [](void* raw) -> uint8_t { sceneLeaf(1); assert(raw == sceneSection); return 1; };
  legacyBook.section.section_partial = [](void* raw) -> uint8_t { sceneLeaf(1); assert(raw == sceneSection); return 0; };
  legacyBook.section.section_load_cache = [](void* raw, const XpEpubRenderSpec*) -> uint8_t { sceneLeaf(1); assert(raw == sceneSection); return 1; };
  legacyBook.section.section_toc_for_page = [](void* raw, void*, int32_t, int32_t) -> int32_t { sceneLeaf(1); assert(!raw || raw == sceneSection); return 0; };
  legacyBook.section.section_offset_for_page = [](void* raw, uint16_t, uint32_t* offset) -> uint8_t { sceneLeaf(1); assert(raw == sceneSection); *offset = 0; return 1; };
  legacyBook.section.section_load_page = [](void* raw, int32_t page) -> void* { sceneLeaf(1); assert(raw == sceneSection); return new ScenePage{page}; };
  legacyBook.section.page.page_info = [](void* page, XpEpubPageInfo* out) { sceneLeaf(1); assert(page); *out = {}; };
  legacyBook.section.page.page_close = [](void* page) { sceneLeaf(1); assert(page && scene.boundPage != page); delete static_cast<ScenePage*>(page); };
  legacyBook.section.page.metadata_assign = [](void*, void* page) -> uint8_t { sceneLeaf(1); assert(page); return 1; };
  legacyBook.section.page.metadata_clear = [](void*) { sceneLeaf(1); };
  leaves.bookmarks_count = [](void*) -> uint32_t { sceneLeaf(); return 0; };
  leaves.clippings_count = []() -> uint32_t { sceneLeaf(); return 0; };
  leaves.progress_save = [](void*, const XpEpubPosition* position) -> uint8_t { sceneLeaf(1); assert(position); return 1; };
  leaves.paint_status = [](const XpEpubStatus* status) { sceneLeaf(1); assert(status); };
  leaves.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) { sceneLeaf(); assert(!peer); *out = {}; };
  leaves.radio_settle_start = [] { sceneLeaf(1); };
  leaves.radio_after_paint = [] { sceneLeaf(1); };
  leaves.fonts_take_corrupt_notice = []() -> uint8_t { sceneLeaf(1); return 0; };
  leaves.trusted_now = []() -> int64_t { sceneLeaf(); return 1000; };
  leaves.state_set_open_path = [](XpAppText path) { sceneLeaf(0); assert(text(path) == f().path); };
  leaves.state_save = [] { sceneLeaf(); };
  legacyBook.recents_add = [](void* book) { sceneLeaf(0); assert(book == f().extractor); };
  leaves.emit_open = [](XpAppText path) { sceneLeaf(0); assert(text(path) == f().path); };
  static auto entryLog = leaves.log;
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    sceneLeaf();
    if (text(origin) == "RUI") {
      assert(severity == 1 && text(message) == "Cannot build reader toolbar");
    } else entryLog(severity, origin, message);  // Actual MEM/ACT/ERS entry and exit messages.
  };
  // Lifecycle uses the inherited physical file/book/component tables unchanged.
  leaves.toolbar_open = []() -> void* { sceneLeaf(0); assert(!scene.toolbar); return scene.toolbar = new ReaderToolbarUi(*target); };
  leaves.toolbar_close = [](void* raw) { sceneLeaf(1); assert(raw == scene.toolbar); delete scene.toolbar; scene.toolbar = nullptr; };
  leaves.toolbar_begin = [](void* raw) { sceneLeaf(); assert(raw == scene.toolbar); scene.toolbar->begin(); };
  leaves.toolbar_paint = [](void* raw, const XpEpubToolbarModel* model, XpEpubRowSource rows, void* context) -> uint8_t {
    sceneLeaf(1); assert(raw == scene.toolbar);
    if (!scene.explicitBuild) ++scene.redrawn;
    const uint8_t result = sceneNativePaint(raw, model, rows, context);
    if (!result) ++scene.notifications;
    assert(!scene.toolbar->model_.rowFill && !scene.toolbar->model_.rowFillContext);
    return result;
  };
  leaves.toolbar_route = [](void* raw, XpEpubRouted* out) {
    sceneLeaf(0); ++scene.routes; if (scene.dispatchInjections) ++scene.afterFailureRoutes;
    sceneNativeRoute(raw, out);
  };
  leaves.toolbar_scroll = sceneNativeScroll;
  leaves.toolbar_visible_rows = sceneNativeVisible;
  leaves.toolbar_reset_navigation = sceneNativeReset;
  leaves.popup_active = [](void* raw) -> uint8_t {
    sceneLeaf(); auto it = f().handles.find(raw); assert(it != f().handles.end());
    assert(it->second->kind == Kind::OverlayPopup || it->second->kind == Kind::FailurePopup);
    return it->second->kind == Kind::OverlayPopup && scene.popupActive;
  };
  leaves.popup_dismiss = [](void*) { sceneLeaf(); scene.popupActive = false; };
  leaves.popup_input = [](void*, XpEpubPopupInput* out) { sceneLeaf(0); ++scene.popupInputs; *out = {}; };
  leaves.input_facts = [](XpEpubInputFacts* out) { sceneLeaf(); *out = {}; out->touch = out->board_touch = 1; };
  leaves.input_services = []() -> const XpUiReaderInputServices* {
    sceneLeaf();
    static XpUiReaderInputServices physical;
    physical = ReaderUtils::inputServices(*mapped);
    static const XpUiReaderInputServices checked{
      &physical,
      [](void* context, uint32_t button, uint8_t query, uint32_t threshold) -> uint8_t {
        sceneLeaf(0); ++scene.sampled; if (scene.dispatchInjections) ++scene.afterFailureSamples;
        const auto& original = *static_cast<XpUiReaderInputServices*>(context);
        return original.button(original.context, button, query, threshold);
      },
      [](void* context, uint8_t query) -> uint32_t {
        sceneLeaf(); if (query == XP_UI_READER_INPUT_SWIPE) ++scene.swipes;
        const auto& original = *static_cast<XpUiReaderInputServices*>(context);
        return original.read(original.context, query);
      },
      [](void* context, int32_t* x, int32_t* y) -> uint8_t {
        sceneLeaf(0); const auto& original = *static_cast<XpUiReaderInputServices*>(context);
        return original.tap(original.context, x, y);
      }};
    return &checked;
  };
  resources.translate = [](uint16_t) -> XpAppText {
    sceneLeaf(); return {reinterpret_cast<const uint8_t*>("Fixture"), 7};
  };
  platform = {&legacy, &resources, &runtime, &leaves};
}
