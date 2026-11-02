#include "idle_session_fixture.h"

static EpubReaderActivity* activeIdle;
static unsigned idleModelDepth;
static EpubReaderActivity& idleOwner() {
  assert(activeIdle && !idleModelDepth && !lockDepth);
  return *activeIdle;
}
static XpUiReaderIdleSnapshot idleSeedSnapshot(const EpubReaderActivity& owner) {
  XpUiReaderIdleSnapshot sample{};
  sample.spine = owner.currentSpineIndex;
  sample.spine_count = owner.epub ? owner.epub->count : 0;
  sample.confirm_threshold = owner.confirmThreshold;
  sample.busy = busy;
  sample.has_epub = bool(owner.epub);
  sample.load_popup = owner.loadFailurePopup.active;
  sample.remembered = owner.baseModel.book_remembered;
  sample.end_options = bool(owner.endOfBookOptions);
  sample.touch = owner.mappedInput.touch;
  sample.remote = owner.mappedInput.remote;
  sample.home_action = static_cast<uint8_t>(owner.mappedInput.home);
  sample.edges = gpio.edges;
  sample.held_buttons = gpio.pressed;
  sample.overlay = static_cast<uint8_t>(owner.overlay);
  sample.automatic = owner.automaticPageTurnActive;
  sample.pending_turn = owner.pendingManualTurn;
  sample.bookmark_notice = owner.showBookmarkMessage;
  sample.dictionary_notice = owner.showDictionaryMessage;
  sample.font_notice = owner.showFontDamagedMessage;
  sample.clipping_notice = owner.showClippingMessage;
  sample.read_move = owner.pendingReadFolderMove;
  sample.recents_removed = owner.recentsEntryRemoved;
  sample.applied_orientation = owner.appliedOrientation;
  sample.orientation = SETTINGS.orientation;
  sample.long_button = SETTINGS.longPressButtonBehavior;
  sample.short_power = SETTINGS.shortPwrBtn;
  sample.tilt = SETTINGS.tiltPageTurn;
  sample.footnote_depth = static_cast<uint8_t>(owner.footnoteDepth);
  sample.touch_controls = SETTINGS.touchReaderControls;
  return sample;
}
static const XpAppRuntime idleRuntime = [] {
  auto table = overlayUnexpectedRuntime();
  table.model_lock = [] { assert(activeIdle && !idleModelDepth); ++idleModelDepth; };
  table.model_unlock = [] { assert(activeIdle && idleModelDepth == 1); --idleModelDepth; };
  return table;
}();
static const XpAppResources idleResources = overlayUnexpectedResources();
static const XpEpubLeaves idleLeaves = [] {
  auto table = overlayUnexpectedLeaves();
  table.render_busy = []() -> uint8_t { idleOwner(); return RenderLock::peek(); };
  table.book_spine_count = [](void* raw) -> int32_t {
    auto& owner = idleOwner(); assert(raw == owner.epub.get());
    return owner.epub->getSpineItemsCount();
  };
  table.popup_active = [](void* raw) -> uint8_t {
    auto& owner = idleOwner(); assert(raw == &owner.loadFailurePopup);
    return owner.loadFailurePopup.isActive();
  };
  table.input_facts = [](XpEpubInputFacts* out) {
    auto& owner = idleOwner();
    *out = {};
    out->held_ms = owner.mappedInput.getHeldTime();
    out->touch = owner.mappedInput.hasTouch();
    out->remote_click = owner.mappedInput.hasRemoteClick();
    out->home_action = static_cast<uint8_t>(owner.mappedInput.homeButtonAction());
    out->raw_edges = gpio.wasAnyPressed() || gpio.wasAnyReleased();
    for (uint8_t button = HalGPIO::BTN_BACK; button <= HalGPIO::BTN_POWER; ++button)
      if (gpio.isPressed(button)) out->raw_held |= 1u << button;
    out->board_touch = 0;
  };
  table.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    auto& owner = idleOwner(); assert(!resolve);
    *out = {};
    out->orientation = SETTINGS.orientation;
    out->long_button = SETTINGS.longPressButtonBehavior;
    out->short_power = SETTINGS.shortPwrBtn;
    out->tilt = SETTINGS.tiltPageTurn;
    out->touch_controls = SETTINGS.touchReaderControls;
    // The old fixture's threshold stub exposes only 0 and 400 milliseconds.
    assert(owner.confirmThreshold == 0 || owner.confirmThreshold == 400);
    out->long_menu = owner.confirmThreshold == 400 ? CrossPointSettings::LP_MENU_BOOKMARK
                                                  : CrossPointSettings::LP_MENU_DISABLED;
  };
  table.input_screen_long = [](int32_t* x, int32_t* y) -> uint8_t {
    return idleOwner().mappedInput.wasScreenLongPress(*x, *y);
  };
  table.input_services = []() -> const XpUiReaderInputServices* {
    idleOwner();
    static const XpUiReaderInputServices input{
      nullptr,
      [](void*, uint32_t mask, uint8_t query, uint32_t threshold) -> uint8_t {
        auto& owner = idleOwner(); assert(mask == XP_UI_BACK);
        if (query == 2) return owner.mappedInput.wasLongPressed(MappedInputManager::Button::Back, threshold);
        assert(query == 1 && threshold == 0);
        // Back release itself performs the fixture's second consuming BackGesture probe.
        return owner.mappedInput.wasReleased(MappedInputManager::Button::Back);
      },
      [](void*, uint8_t query) -> uint32_t {
        auto& owner = idleOwner(); assert(query == 8);
        return owner.mappedInput.wasBackGesture();
      },
      &OverlayUnexpected<decltype(XpUiReaderInputServices::tap)>::call,
    };
    return &input;
  };
  return table;
}();

struct IdleSessionBridge {
  void* rust;
  explicit IdleSessionBridge(EpubReaderActivity& owner) {
    assert(!activeIdle && !idleModelDepth && !lockDepth);
    activeIdle = &owner;
    XpEpubTestIdleSeed seed{&owner, owner.epub.get(), &owner.loadFailurePopup, idleSeedSnapshot(owner)};
    rust = xp_epub_test_idle_create(&seed, &idleLeaves, &idleRuntime, &idleResources);
    assert(rust);
  }
  ~IdleSessionBridge() {
    idleOwner();
    xp_epub_test_idle_destroy(rust);
    activeIdle = nullptr;
  }
  bool tryIdle() {
    const uint8_t result = xp_epub_test_idle(rust);
    idleOwner(); assert(result <= 1);
    return result != 0;
  }
};

bool EpubReaderActivity::tryIdleLoop() {
  IdleSessionBridge bridge(*this);
  return bridge.tryIdle();
}
