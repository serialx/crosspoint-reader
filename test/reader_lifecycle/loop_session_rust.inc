#include "loop_session_fixture.h"

namespace loop_session {
struct Bridge;
static Bridge* active;
static unsigned modelDepth;
static LegacyEpubLeaves leaves;
static XpAppRuntime runtime;
static XpAppResources resources;

struct Bridge {
  EpubReaderActivity& owner;
  void* rust = nullptr;
  bool creating = true, destroying = false;
  unsigned publication = 0, publications = 0, buildAttempts = 0;
  unsigned sectionCloses = 0, pagesOpened = 0, pagesClosed = 0;
  bool seededSection;
  Page* page = nullptr;
  std::optional<FontCache::Scope> prewarm;
  bool scanFinished = false;
  const Link* link = nullptr;
  explicit Bridge(EpubReaderActivity&);
  ~Bridge() {
    assert(active == this && !modelDepth && !lockDepth && !lockInstances && !page && !prewarm && !publication);
    destroying = true;
    xp_epub_test_loop_destroy(rust);
    assert(!owner.section && sectionCloses == unsigned(seededSection));
    assert(publications == unsigned(seededSection) + buildAttempts);
    assert(pagesOpened == pagesClosed);
    active = nullptr;
  }
  void step() {
    assert(!creating && !destroying && !publication && !page && !prewarm);
    xp_epub_test_loop(rust);
    assert(!modelDepth && !lockDepth && !lockInstances && !publication && !page && !prewarm);
  }
  void printState() const {
    assert(!modelDepth && !lockDepth && !lockInstances);
    XpEpubTestLoopObservation state{};
    xp_epub_test_loop_observe(rust, &state);
    std::printf("state %d %d %d %d %d %d %d %lu %d %d %d %d %d %d %d %d %d %u %u\n",
                state.radio_released, state.idle_spine, state.idle_page, state.partial_failed, state.recents_removed,
                state.read_move, state.overlay, static_cast<unsigned long>(state.last_turn), state.automatic,
                state.manual_turn, state.manual_touch, state.bookmark, state.dictionary, state.font, state.clipping,
                state.orientation, state.spine, owner.mappedInput.fired, owner.mappedInput.suppressed);
  }
};
static Bridge& bridge() {
  assert(active && !modelDepth);
  return *active;
}
static Bridge& context(void* raw) {
  auto& value = bridge(); assert(raw == active); return value;
}
static void unlocked() { bridge(); assert(!lockDepth && !lockInstances); }
static void locked() { bridge(); assert(lockDepth == 1 && lockInstances == 1); }
static Section& section(void* raw) {
  auto& value = bridge();
  assert(raw && raw == value.owner.section.get());
  if (value.creating || value.destroying) assert(!lockDepth && !lockInstances);
  else locked();
  return *value.owner.section;
}
static void publicationRead(unsigned expected) {
  auto& value = bridge(); assert(value.publication == expected);
  if (++value.publication == 6) { value.publication = 0; ++value.publications; }
}
static XpAppText text(const std::string& value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
static std::string string(XpAppText value) {
  assert(value.data || !value.len);
  return value.len ? std::string(reinterpret_cast<const char*>(value.data), value.len) : std::string{};
}
static MappedInputManager::Button button(uint32_t mask) {
  using Button = MappedInputManager::Button;
  switch (mask) {
    case XP_UI_BACK: return Button::Back;
    case XP_UI_CONFIRM: return Button::Confirm;
    case XP_UI_POWER: return Button::Power;
    case XP_UI_DOWN: return Button::Down;
    default: assert(false && "unexpected loop button"); std::abort();
  }
}
static void configure() {
  leaves = overlayLegacyLeaves();
  leaves.physical.book_set_extractor = [](void* handle) {
    if (handle) leaves.book.book_install_extractor(handle);
    else leaves.book.book_clear_extractor();
  }; runtime = overlayUnexpectedRuntime(); resources = overlayUnexpectedResources();
  runtime.model_lock = [] { assert(active && !modelDepth); ++modelDepth; };
  runtime.model_unlock = [] { assert(active && modelDepth == 1); --modelDepth; };
  runtime.render_lock = []() -> void* { unlocked(); return new RenderLock; };
  runtime.render_try_lock = []() -> void* {
    unlocked(); auto value = std::make_unique<RenderLock>(RenderLock::Mode::Try);
    if (!value->ownsLock()) return nullptr;
    return value.release();
  };
  runtime.render_unlock = [](void* raw) {
    locked(); assert(raw && !bridge().page && !bridge().prewarm && !bridge().publication);
    delete static_cast<RenderLock*>(raw);
  };
  runtime.now_ms = []() -> uint32_t { bridge(); assert(lockDepth <= 1 && lockInstances == lockDepth); return millis(); };
  runtime.touch_feedback = [](uint8_t extended) { unlocked(); haptic_feedback::touchAction(extended != 0); };
  leaves.physical.render_busy = []() -> uint8_t { unlocked(); return RenderLock::peek(); };
  leaves.physical.popup_active = [](void* raw) -> uint8_t {
    auto& owner = bridge().owner; unlocked(); assert(raw == &owner.loadFailurePopup);
    return owner.loadFailurePopup.isActive();
  };
  leaves.physical.popup_input = [](void* raw, XpEpubPopupInput* out) {
    auto& owner = bridge().owner; unlocked(); assert(raw == &owner.loadFailurePopup);
    bool changed = false;
    owner.loadFailurePopup.handleInput(owner.mappedInput, [&] { changed = true; });
    *out = {uint8_t(owner.loadFailurePopup.active), uint8_t(changed), 1};
  };
  leaves.book.book_spine_count = [](void* raw) -> int32_t {
    auto& owner = bridge().owner; unlocked(); assert(raw == owner.epub.get());
    return owner.epub->getSpineItemsCount();
  };
  leaves.book.book_path = [](void* raw, XpAppTextSink sink, void* destination) -> uint8_t {
    auto& owner = bridge().owner; unlocked(); assert(raw == owner.epub.get());
    const auto value = owner.epub->getPath(); return sink(destination, text(value));
  };
  leaves.physical.recents_remove = [](XpAppText path) -> uint8_t {
    unlocked(); assert(string(path) == bridge().owner.epub->getPath());
    return RECENT_BOOKS.removeByPath(string(path));
  };
  leaves.book.recents_add = [](void* raw) {
    auto& owner = bridge().owner; unlocked(); assert(raw == owner.epub.get());
    RECENT_BOOKS.addBook(owner.epub->getPath(), owner.epub->getTitle(), owner.epub->getAuthor(),
                         owner.epub->getThumbBmpPath());
  };
  leaves.book.section.section_page_count = [](void* raw) -> uint16_t {
    auto& value = section(raw);
    if (bridge().publication) publicationRead(1);
    return static_cast<uint16_t>(value.pageCount);
  };
  leaves.book.section.section_estimated_pages = [](void* raw) -> int32_t {
    auto& value = section(raw); publicationRead(2); return value.pageCount;
  };
  leaves.book.section.section_building = [](void* raw) -> uint8_t {
    auto& value = section(raw);
    if (bridge().publication) { publicationRead(3); return value.building; }
    return value.isBuilding();
  };
  leaves.book.section.section_complete = [](void* raw) -> uint8_t { section(raw); publicationRead(4); return 0; };
  leaves.book.section.section_partial = [](void* raw) -> uint8_t {
    auto& value = section(raw);
    if (bridge().publication) { publicationRead(5); return value.partial; }
    return value.isPartial();
  };
  leaves.book.section.section_close = [](void* raw) {
    section(raw); auto& value = bridge(); assert(value.destroying && !value.page && !value.prewarm);
    assert(!value.sectionCloses++); value.owner.section.reset();
  };
  leaves.book.section.section_load_page = [](void* raw, int32_t index) -> void* {
    auto& value = section(raw); auto& owner = bridge();
    assert(!owner.creating && !owner.page && !owner.prewarm && !owner.publication);
    auto page = value.loadPage(index);
    owner.page = page.get(); if (page) ++owner.pagesOpened;
    return page.release();
  };
  leaves.book.section.page.page_close = [](void* raw) {
    auto& owner = bridge(); locked(); assert(raw && raw == owner.page && !owner.prewarm);
    delete static_cast<Page*>(raw); owner.page = nullptr; ++owner.pagesClosed;
  };
  leaves.book.section.section_start_build = [](void* raw, const XpEpubRenderSpec* spec, XpEpubBuildPopup popup, void* ctx) -> uint8_t {
    auto& value = section(raw); auto& owner = bridge();
    assert(!owner.creating && !owner.publication && !popup && !ctx && spec && spec->font == 1);
    assert(spec->width == owner.owner.buildViewportWidth && spec->height == owner.owner.buildViewportHeight);
    const bool result = value.startBuild(spec->font);
    owner.publication = 1; ++owner.buildAttempts;
    return result;
  };
  leaves.book.section.page.strip_release_idle = [](void* raw) -> uint8_t { context(raw); locked(); return 0; };
  leaves.physical.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    bridge(); *out = {};
    out->orientation = SETTINGS.orientation; out->long_menu = SETTINGS.longPressMenuFunction;
    out->short_power = SETTINGS.shortPwrBtn; out->long_button = SETTINGS.longPressButtonBehavior;
    out->remove_read_recents = SETTINGS.removeReadBooksFromRecents;
    out->move_finished = SETTINGS.moveFinishedToReadFolder; out->touch_controls = SETTINGS.touchReaderControls;
    if (resolve) { locked(); out->font = SETTINGS.getReaderFontId(); }
  };
  leaves.physical.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    locked(); *out = {}; out->font = SETTINGS.readerRenderSpec(width, height); out->width = width; out->height = height;
  };
  leaves.physical.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    locked(); assert(!peer); *out = {}; out->note = static_cast<uint8_t>(bleturner::linkNote());
  };
  leaves.physical.radio_holds_heap = []() -> uint8_t { locked(); return bleturner::holdsHeap(); };
  leaves.physical.radio_after_paint = [] { locked(); bleturner::afterPaint(); };
  leaves.physical.render_geometry = [](XpEpubGeometry* out) {
    auto& owner = bridge().owner; locked(); *out = {};
    out->has_framebuffer = owner.renderer.frame; out->has_font_cache = owner.renderer.font;
  };
  leaves.book.section.page.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    context(raw); locked();
    static const XpUiReaderRenderServices services{
      nullptr,
      [](void*, uint8_t query) -> uint32_t {
        locked();
        if (query == XP_UI_READER_RENDER_FREE_HEAP) return ESP.getFreeHeap();
        assert(query == XP_UI_READER_RENDER_MAX_BLOCK); return ESP.getMaxAllocHeap();
      },
      &OverlayUnexpected<decltype(XpUiReaderRenderServices::execute)>::call,
    };
    return &services;
  };
  leaves.book.section.page.fonts_prewarm_begin = [](void* raw) -> uint8_t {
    auto& value = context(raw); locked(); assert(value.page && !value.prewarm && value.owner.renderer.font);
    value.prewarm.emplace(value.owner.renderer.cache.createPrewarmScope()); value.scanFinished = false; return 1;
  };
  leaves.book.section.page.fonts_prewarm_finish_scan = [](void* raw) {
    auto& value = context(raw); locked(); assert(value.page && value.prewarm && !value.scanFinished);
    value.prewarm->endScanAndPrewarm(); value.scanFinished = true;
  };
  leaves.book.section.page.fonts_prewarm_drop = [](void* raw) {
    auto& value = context(raw); locked(); assert(value.page && value.prewarm && value.scanFinished);
    value.prewarm.reset();
  };
  leaves.book.section.page.page_render = [](void* raw, int32_t font, int32_t left, int32_t top) {
    auto& value = bridge(); locked(); assert(raw == value.page && value.prewarm && !value.scanFinished);
    assert(font == 1 && !left && !top); value.page->render(value.owner.renderer, font, left, top);
  };
  leaves.book.section.page.metadata_link = [](void* raw, uint16_t index, XpEpubLinkSink sink, void* destination) -> uint8_t {
    auto& value = context(raw); locked(); assert(index == 0 && value.link);
    XpEpubLink row{}; row.href = text(value.link->href); return sink(destination, &row);
  };
  leaves.physical.input_services = []() -> const XpUiReaderInputServices* {
    unlocked();
    static const XpUiReaderInputServices input{
      nullptr,
      [](void*, uint32_t mask, uint8_t query, uint32_t threshold) -> uint8_t {
        auto& owner = bridge().owner; unlocked();
        if (query == 2) return owner.mappedInput.wasLongPressed(button(mask), threshold);
        assert(query == 1 && !threshold); return owner.mappedInput.wasReleased(button(mask));
      },
      [](void*, uint8_t query) -> uint32_t {
        auto& owner = bridge().owner; unlocked();
        switch (query) {
          case 0: return owner.mappedInput.hasTouch();
          case 2: return owner.mappedInput.getHeldTime();
          case 7: return static_cast<uint32_t>(owner.mappedInput.homeButtonAction());
          default: assert(false && "unexpected mapped query"); std::abort();
        }
      },
      &OverlayUnexpected<decltype(XpUiReaderInputServices::tap)>::call,
    };
    return &input;
  };
  leaves.physical.input_screen_long = [](int32_t* x, int32_t* y) -> uint8_t {
    unlocked(); return bridge().owner.mappedInput.wasScreenLongPress(*x, *y);
  };
  leaves.physical.input_screen_tap = [](int32_t* x, int32_t* y) -> uint8_t {
    unlocked(); return bridge().owner.mappedInput.wasScreenTapped(*x, *y);
  };
  leaves.physical.log_enabled = [](uint8_t severity) -> uint8_t { bridge(); assert(severity <= 1); return severity || LOOP_LOG_LEVEL >= 2; };
  leaves.physical.log = [](uint8_t severity, XpAppText, XpAppText) {
    bridge(); assert(severity <= 1);
    if (severity) error(); else { assert(LOOP_LOG_LEVEL >= 2); debug(); }
  };
}

Bridge::Bridge(EpubReaderActivity& value) : owner(value), seededSection(bool(value.section)) {
  assert(!active && !modelDepth && !lockDepth && !lockInstances); active = this; configure();
  XpEpubTestLoopSeed seed{};
  seed.native = this; seed.book = owner.epub.get(); seed.section = owner.section.get(); seed.popup = &owner.loadFailurePopup;
  auto& state = seed.state;
  state.spine = owner.currentSpineIndex; state.page = owner.section ? owner.section->currentPage : 0;
  state.idle_spine = owner.idlePrewarmSpine; state.idle_page = owner.idlePrewarmPage;
  state.last_render = owner.lastRenderCompleteMs; state.last_turn = owner.lastPageTurnTime; state.duration = owner.pageTurnDuration;
  state.bookmark_time = owner.bookmarkMessageTime; state.dictionary_time = owner.dictionaryMessageTime;
  state.font_time = owner.fontDamagedMessageTime; state.clipping_time = owner.clippingMessageTime;
  state.link_left = owner.currentPageLinkMarginLeft; state.link_top = owner.currentPageLinkMarginTop;
  state.width = owner.buildViewportWidth; state.height = owner.buildViewportHeight; state.orientation = owner.appliedOrientation;
  state.radio_released = owner.radioReleasedForBuild.load(); state.partial_failed = owner.partialRebuildStartFailed;
  state.recents_removed = owner.recentsEntryRemoved; state.read_move = owner.pendingReadFolderMove;
  state.overlay = static_cast<uint8_t>(owner.overlay); state.automatic = owner.automaticPageTurnActive;
  state.manual_turn = owner.pendingManualTurn; state.manual_touch = owner.pendingManualTurnTouch;
  state.bookmark = owner.showBookmarkMessage; state.dictionary = owner.showDictionaryMessage;
  state.font = owner.showFontDamagedMessage; state.clipping = owner.showClippingMessage; state.history_depth = owner.footnoteDepth;
  auto& hooks = seed.hooks; hooks.context = this;
  hooks.idle = [](void* raw) -> uint8_t { unlocked(); return context(raw).owner.tryIdleLoop(); };
  hooks.remember = [](void* raw) { unlocked(); context(raw).owner.rememberBookOnceRendered(); };
  hooks.clear_end = [](void* raw) { unlocked(); context(raw).owner.clearEndOfBookOptionsIfNeeded(); };
  hooks.touch_menu = [](void* raw) -> uint8_t {
    unlocked(); auto& owner = context(raw).owner; return ReaderUtils::isTouchMenuGesture(owner.renderer, owner.mappedInput);
  };
  hooks.overlay_input = [](void* raw) { unlocked(); context(raw).owner.handleOverlayInput(); };
  hooks.restore = [](void* raw) { unlocked(); context(raw).owner.restoreSavedPosition(); };
  hooks.background = [](void* raw) { unlocked(); context(raw).owner.backgroundBuildTick(); };
  hooks.end_menu = [](void* raw) -> uint8_t { unlocked(); return context(raw).owner.handleEndOfBookMenu(); };
  hooks.end_active = [](void* raw) -> uint8_t { unlocked(); return context(raw).owner.endOfBookMenuActive(); };
  hooks.threshold = [](void* raw) -> uint32_t { unlocked(); return context(raw).owner.confirmLongPressThreshold(); };
  hooks.end_turn = [](void* raw, uint8_t previous, uint8_t next) -> uint8_t {
    unlocked(); return context(raw).owner.handleEndOfBookPageTurn(previous != 0, next != 0);
  };
  hooks.sync = [](void* raw) -> uint8_t { unlocked(); return context(raw).owner.launchKOReaderSync(); };
  hooks.link_repaint = [](void* raw, uint8_t note) -> uint8_t {
    locked(); return context(raw).owner.linkNoteTitle.repaint(note);
  };
  hooks.link_hit = [](void* raw, int32_t x, int32_t y, int32_t left, int32_t top) -> int32_t {
    auto& value = context(raw); locked();
    value.link = EpubReaderUtils::linkAtPoint(value.owner.currentPageMetadata.links(), x, y, left, top);
    return value.link ? 0 : -1;
  };
  hooks.framebuffer = [](void* raw, uint8_t actual) {
    auto& value = context(raw); locked(); assert(value.owner.renderer.hasFrameBuffer() == bool(actual));
  };
  hooks.font_cache = [](void* raw, uint8_t actual) {
    auto& value = context(raw); locked(); assert((value.owner.renderer.getFontCacheManager() != nullptr) == bool(actual));
  };
  hooks.read_folder = [](void* raw, uint8_t actual) {
    auto& value = context(raw); unlocked(); assert(isInReadFolder(value.owner.epub->getPath()) == bool(actual));
  };
  hooks.orientation = [](void* raw, uint8_t orientation) { unlocked(); context(raw).owner.applyOrientation(orientation); };
  seed.finish = [](void* raw) { unlocked(); context(raw).owner.finish(); };
  seed.update = [](void* raw) { context(raw).owner.requestUpdate(); };
  seed.buttons = [](void* raw) -> XpUiReaderTurnInput {
    unlocked(); const auto result = ReaderUtils::detectPageTurn(context(raw).owner.mappedInput);
    return {0, uint8_t(std::get<0>(result)), uint8_t(std::get<1>(result)), uint8_t(std::get<2>(result)), 0};
  };
  seed.touch = [](void* raw) -> XpUiReaderTurnInput {
    unlocked(); auto& owner = context(raw).owner;
    const bool rtl = ReaderUtils::isRtlBookLanguage(owner.epub->getLanguage());
    const auto result = ReaderUtils::detectTouchPageTurn(owner.renderer, owner.mappedInput, rtl);
    return {static_cast<uint32_t>(result.heldMs), uint8_t(result.prev), uint8_t(result.next), 0, 0};
  };
  seed.back = [](void* raw) -> uint8_t { unlocked(); return context(raw).owner.handleBackNavigation(); };
  seed.turn = [](void* raw, uint8_t forward) -> uint8_t { unlocked(); return context(raw).owner.pageTurn(forward != 0); };
  seed.skip = [](void* raw, int32_t amount) -> uint8_t { unlocked(); return context(raw).owner.skipPages(amount); };
  seed.note = [](void* raw, uint8_t forward, uint8_t success) {
    unlocked(); context(raw).owner.notePageTurn(forward != 0, success != 0);
  };
  seed.toolbar = [](void* raw) -> uint8_t { unlocked(); return context(raw).owner.usesToolbarMenu(); };
  seed.bookmark = [](void* raw) { unlocked(); context(raw).owner.addBookmark(); };
  seed.dictionary = [](void* raw) { unlocked(); context(raw).owner.openDictionaryWordSelect(); };
  seed.footnotes = [](void* raw, uint8_t restore) { unlocked(); context(raw).owner.openFootnoteSelect(restore != 0); };
  seed.selection = [](void* raw, int32_t x, int32_t y, uint8_t mode) {
    unlocked(); assert(mode == 0); context(raw).owner.startClipSelection(x, y);
  };
  seed.menu = [](void* raw) { unlocked(); context(raw).owner.openReaderMenu(); };
  seed.navigate = [](void* raw, XpAppText href, uint8_t save) {
    unlocked(); assert(string(href) == "target" && save == 1); context(raw).owner.navigateToHref(string(href), save != 0);
  };
  seed.overlay = [](void* raw, uint8_t overlay) {
    unlocked(); context(raw).owner.openOverlay(static_cast<EpubReaderActivity::Overlay>(overlay));
  };
  seed.discard = [](void* raw) { locked(); context(raw).owner.discardOverlayPage(); };
  publication = seededSection ? 1 : 0;
  rust = xp_epub_test_loop_create_legacy_book(&seed,&leaves.physical, &runtime, &resources, &leaves.book);
  assert(rust && !publication && publications == unsigned(seededSection)); creating = false;
}
}  // namespace loop_session

void EpubReaderActivity::loop() {
  assert(&loop_session::bridge().owner == this);
  loop_session::bridge().step();
}
