#include "overlay_input_session_fixture.h"

struct OverlayInputSessionBridge;
static OverlayInputSessionBridge* activeOverlayInput;
static unsigned overlayInputModelDepth;
static LegacyEpubLeaves overlayInputLeaves;
static XpAppRuntime overlayInputRuntime;
static XpAppResources overlayInputResources;

struct OverlayInputSessionBridge {
  EpubReaderActivity& owner;
  void* rust = nullptr;
  explicit OverlayInputSessionBridge(EpubReaderActivity&);
  ~OverlayInputSessionBridge() {
    assert(activeOverlayInput == this && !overlayInputModelDepth && !lockDepth);
    xp_epub_test_overlay_destroy(rust);
    activeOverlayInput = nullptr;
  }
  void observe() {
    assert(activeOverlayInput == this && rust && !overlayInputModelDepth);
    XpEpubTestOverlayObservation base{};
    XpEpubTestOverlayInputObservation input{};
    xp_epub_test_overlay_observe(rust, &base);
    xp_epub_test_overlay_input_observe(rust, &input);
    assert(!base.failed && bool(base.toolbar) == bool(owner.toolbarUi));
    assert(bool(input.section) == bool(owner.section));
    owner.overlay = static_cast<EpubReaderActivity::Overlay>(base.overlay);
    owner.focusedTool = base.focus;
    owner.panelIndex = base.index;
    owner.panelCursorShown = base.cursor;
    owner.panelHoldJumped = base.hold;
    owner.overlayPageStored = base.stored;
    owner.currentSpineIndex = input.spine;
    owner.nextPageNumber = input.next_page;
    xp_epub_test_overlay_input_anchor(rust, [](void* raw, XpAppText text) -> uint8_t {
      assert(text.data || !text.len);
      auto& anchor = static_cast<OverlayInputSessionBridge*>(raw)->owner.pendingAnchor;
      anchor = text.len ? std::string(reinterpret_cast<const char*>(text.data), text.len) : std::string{};
      return 1;
    }, this);
  }
  void input() {
    xp_epub_test_overlay_input(rust);
    observe();
    assert(!lockDepth && !overlayInputModelDepth);
  }
  void deliver(const ActivityResult& result) {
    XpAppResultView view{};
    view.original = &result;
    xp_epub_test_overlay_input_result(rust, &view);
    observe();
  }
  void push(void* child) {
    assert(child && !lockDepth && !overlayInputModelDepth);
    owner.startActivityForResult(std::unique_ptr<TextSettingsActivity>(static_cast<TextSettingsActivity*>(child)),
                                 [this](const ActivityResult& result) { deliver(result); });
  }
};

static OverlayInputSessionBridge& overlayInputBridge(void* context) {
  assert(activeOverlayInput && !overlayInputModelDepth && context == activeOverlayInput);
  return *activeOverlayInput;
}
static EpubReaderActivity& overlayInputOwner() {
  assert(activeOverlayInput && !overlayInputModelDepth);
  return activeOverlayInput->owner;
}
static MappedInputManager::Button overlayInputButton(uint32_t mask) {
  using Button = MappedInputManager::Button;
  switch (mask) {
    case XP_UI_BACK: return Button::Back;
    case XP_UI_LEFT: return Button::Left;
    case XP_UI_RIGHT: return Button::Right;
    case XP_UI_CONFIRM: return Button::Confirm;
    case XP_UI_UP: return Button::Up;
    case XP_UI_DOWN: return Button::Down;
    default: assert(false && "unexpected mapped button"); std::abort();
  }
}

static void configureOverlayInputLeaves() {
  overlayInputLeaves = overlayLegacyLeaves();
  overlayInputLeaves.physical.book_set_extractor = [](void* handle) {
    if (handle) overlayInputLeaves.book.book_install_extractor(handle);
    else overlayInputLeaves.book.book_clear_extractor();
  };
  overlayInputRuntime = overlayUnexpectedRuntime();
  overlayInputResources = overlayUnexpectedResources();
  overlayInputRuntime.model_lock = [] { assert(activeOverlayInput && !overlayInputModelDepth); ++overlayInputModelDepth; };
  overlayInputRuntime.model_unlock = [] { assert(overlayInputModelDepth == 1); --overlayInputModelDepth; };
  overlayInputRuntime.render_lock = []() -> void* {
    overlayInputOwner(); assert(!lockDepth); return new RenderLock;
  };
  overlayInputRuntime.render_unlock = [](void* raw) {
    overlayInputOwner(); assert(raw && lockDepth == 1); delete static_cast<RenderLock*>(raw);
  };
  overlayInputLeaves.physical.input_services = []() -> const XpUiReaderInputServices* {
    overlayInputOwner(); assert(!lockDepth);
    static const XpUiReaderInputServices input{
      nullptr,
      [](void*, uint32_t button, uint8_t query, uint32_t threshold) -> uint8_t {
        auto& input = overlayInputOwner().mappedInput;
        assert(!lockDepth && threshold == 0);
        auto& frame = input;
        const auto nativeButton = overlayInputButton(button);
        // NATIVE_BUTTON_QUERY
      },
      [](void*, uint8_t query) -> uint32_t {
        auto& input = overlayInputOwner().mappedInput;
        assert(!lockDepth);
        if (query == XP_UI_READER_INPUT_SWIPE) {
          auto& frame = input;
          // NATIVE_SWIPE_QUERY
        }
        assert(query == XP_UI_READER_INPUT_HELD_MS);
        return input.getHeldTime();
      },
      &OverlayUnexpected<decltype(XpUiReaderInputServices::tap)>::call,
    };
    return &input;
  };
  overlayInputLeaves.physical.input_pressed = [](uint32_t index) -> uint8_t {
    assert(!lockDepth);
    return overlayInputOwner().mappedInput.isPressed(inputButton(index));
  };
  overlayInputLeaves.physical.toolbar_route = [](void* raw, XpEpubRouted* out) {
    auto& owner = overlayInputOwner();
    assert(!lockDepth && raw == owner.toolbarUi.get());
    const auto routed = owner.toolbarUi->route(owner.mappedInput);
    *out = {routed.value, routed.permille, routed.x, routed.y, static_cast<uint8_t>(routed.event), uint8_t(routed.routed)};
  };
  overlayInputLeaves.physical.toolbar_visible_rows = [](void* raw) -> int32_t {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == owner.toolbarUi.get());
    return owner.toolbarUi->visibleRows();
  };
  overlayInputLeaves.physical.toolbar_scroll = [](void* raw, int32_t delta, int32_t count) -> uint8_t {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == owner.toolbarUi.get());
    return owner.toolbarUi->nav().scrollBy(delta, count);
  };
  overlayInputLeaves.physical.toolbar_begin = [](void* raw) {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == owner.toolbarUi.get());
    owner.toolbarUi->begin();
  };
  overlayInputLeaves.physical.popup_active = [](void* raw) -> uint8_t {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == &owner.overlayPopup);
    return owner.overlayPopup.isActive();
  };
  overlayInputLeaves.physical.popup_dismiss = [](void* raw) {
    auto& owner = overlayInputOwner(); assert(lockDepth == 1 && raw == &owner.overlayPopup);
    owner.overlayPopup.dismiss();
  };
  overlayInputLeaves.physical.popup_input = [](void* raw, XpEpubPopupInput* out) {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == &owner.overlayPopup);
    bool changed = false;
    owner.overlayPopup.handleInput(owner.mappedInput, [&] { changed = true; });
    *out = {uint8_t(owner.overlayPopup.active), uint8_t(changed), 1};
  };
  overlayInputLeaves.book.book_spine_count = [](void* raw) -> int32_t {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == owner.epub.get());
    return owner.epub->getSpineItemsCount();
  };
  overlayInputLeaves.book.book_toc_count = [](void* raw) -> int32_t {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == owner.epub.get());
    return owner.epub->getTocItemsCount();
  };
  overlayInputLeaves.book.book_toc = [](void* raw, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
    auto& owner = overlayInputOwner(); assert(!lockDepth && raw == owner.epub.get());
    const auto item = owner.epub->getTocItem(index);
    XpEpubToc view{};
    view.spine = item.spineIndex;
    view.anchor = {reinterpret_cast<const uint8_t*>(item.anchor.data()), item.anchor.size()};
    return sink(context, &view);
  };
  overlayInputLeaves.physical.section_close = [](void* raw) {
    auto& owner = overlayInputOwner(); assert(lockDepth == 1 && raw == owner.section.get());
    owner.section.reset();
  };
  // Geometry extraction is pure; the after-extraction hooks retain each lazy probe.
  overlayInputLeaves.physical.render_geometry = [](XpEpubGeometry* out) {
    auto& owner = overlayInputOwner(); assert(!lockDepth);
    *out = {}; out->width = owner.renderer.width; out->height = owner.renderer.height;
  };
  overlayInputLeaves.physical.render_restore_bw = [](uint8_t resync) {
    auto& owner = overlayInputOwner(); assert(lockDepth == 1 && !resync);
    owner.renderer.restoreBwBuffer(resync != 0);
  };
  overlayInputLeaves.physical.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    overlayInputBridge(raw); assert(lockDepth == 1);
    static const XpUiReaderRenderServices render{
      nullptr, &OverlayUnexpected<decltype(XpUiReaderRenderServices::read)>::call,
      [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
        auto& owner = overlayInputOwner(); assert(lockDepth == 1 && op && op->kind == XP_UI_READER_RENDER_STORE_BW);
        return owner.renderer.storeBwBuffer();
      },
    };
    return &render;
  };
  overlayInputLeaves.physical.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    overlayInputOwner(); assert(!lockDepth && !resolve);
    *out = {}; out->focus_reading = SETTINGS.focusReadingEnabled;
  };
  overlayInputLeaves.physical.settings_focus_reading = [](uint8_t value) {
    overlayInputOwner(); assert(!lockDepth); SETTINGS.focusReadingEnabled = value;
  };
  overlayInputLeaves.physical.child_text_settings = []() -> void* {
    auto& owner = overlayInputOwner(); assert(!lockDepth);
    return std::make_unique<TextSettingsActivity>(owner.renderer, owner.mappedInput, &sdFontSystem.registry(),
                                                  TextSettingsActivity::Tab::Family).release();
  };
}

OverlayInputSessionBridge::OverlayInputSessionBridge(EpubReaderActivity& value) : owner(value) {
  assert(!activeOverlayInput && !overlayInputModelDepth && !lockDepth);
  activeOverlayInput = this;
  configureOverlayInputLeaves();
  XpEpubTestOverlayInputSeed seed{};
  auto& base = seed.base;
  base.native = this; base.book = owner.epub.get(); base.section = owner.section.get();
  base.toolbar = owner.toolbarUi.get(); base.popup = &owner.overlayPopup;
  base.focus = owner.focusedTool; base.index = owner.panelIndex;
  base.overlay = static_cast<uint8_t>(owner.overlay);
  base.cursor = owner.panelCursorShown; base.hold = owner.panelHoldJumped; base.stored = owner.overlayPageStored;
  base.context = this;
  base.home_reset = &OverlayUnexpected<decltype(base.home_reset)>::call;
  base.current_toc = &OverlayUnexpected<decltype(base.current_toc)>::call;
  base.build_more = &OverlayUnexpected<decltype(base.build_more)>::call;
  base.a4 = &OverlayUnexpected<decltype(base.a4)>::call;
  base.xteink = &OverlayUnexpected<decltype(base.xteink)>::call;
  base.paint = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(lockDepth == 1); bridge.observe(); bridge.owner.renderOverlay();
  };
  base.push = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(lockDepth == 1); bridge.observe(); bridge.owner.pushOverlayRefresh();
  };
  base.settle = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(lockDepth == 1); bridge.observe(); bridge.owner.settleOverlayRefresh();
  };
  base.discard = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(lockDepth == 1); bridge.observe(); bridge.owner.discardOverlayPage();
  };
  base.update = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); bridge.observe();
    bridge.owner.requestUpdate();
  };
  seed.spine = owner.currentSpineIndex; seed.next_page = owner.nextPageNumber;
  assert(owner.moreItems.size() <= 19); seed.more_count = static_cast<uint8_t>(owner.moreItems.size());
  seed.hooks.context = this;
  seed.hooks.open = [](void* raw, uint8_t target) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe();
    assert(static_cast<uint8_t>(bridge.owner.overlay) == target);
    bridge.owner.openOverlay(static_cast<EpubReaderActivity::Overlay>(target));
  };
  seed.hooks.close = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe();
    assert(bridge.owner.overlay == EpubReaderActivity::Overlay::None); bridge.owner.closeOverlayToPage();
  };
  seed.hooks.clear = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(lockDepth == 1); bridge.observe(); bridge.owner.clearDeferredReposition();
  };
  seed.hooks.text = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe(); bridge.owner.applyReaderTextSettings();
  };
  seed.hooks.live = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe(); bridge.owner.applyTextSettingLive();
  };
  seed.hooks.text_popup = [](void* raw, int32_t row) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe(); bridge.owner.showTextRowPopup(row);
  };
  seed.hooks.more = [](void* raw, int32_t row) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe(); bridge.owner.activateMoreRow(row);
  };
  seed.hooks.popup = [](void* raw) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth); bridge.observe(); bridge.owner.paintOverlayPopup();
  };
  seed.hooks.width = [](void* raw, int32_t value) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth);
    assert(bridge.owner.renderer.getScreenWidth() == value);
  };
  seed.hooks.height = [](void* raw, int32_t value) {
    auto& bridge = overlayInputBridge(raw); assert(!lockDepth);
    assert(bridge.owner.renderer.getScreenHeight() == value);
  };
  seed.push_child = [](void* raw, void* child) { overlayInputBridge(raw).push(child); };
  rust = xp_epub_test_overlay_input_create_legacy_book(&seed, &overlayInputLeaves.physical, &overlayInputRuntime, &overlayInputResources, &overlayInputLeaves.book);
  assert(rust);
}
void EpubReaderActivity::handleOverlayInput() {
  assert(&overlayInputOwner() == this);
  activeOverlayInput->input();
}
