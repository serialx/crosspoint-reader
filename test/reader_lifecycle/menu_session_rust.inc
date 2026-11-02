#include "menu_session_fixture.h"
#include <I18nKeys.h>
#include <cstring>

static int menuModelDepth;
static XpAppText menuText(const std::string& text) {
  return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
}
static XpAppText menuLiteral(const char* text) {
  return {reinterpret_cast<const uint8_t*>(text), std::strlen(text)};
}
static std::string menuString(XpAppText text) {
  assert(text.data || text.len == 0);
  return text.len ? std::string(reinterpret_cast<const char*>(text.data), text.len) : std::string{};
}
static constexpr StrId menuKeys[] = {
    StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED,
    StrId::STR_LANDSCAPE_CCW, StrId::STR_ORIENTATION, StrId::STR_AUTO_TURN_PAGES_PER_MIN,
    StrId::STR_STATE_OFF};
static const char* menuTranslations[] = {"portrait", "landscape-cw", "inverted", "landscape-ccw", "orientation",
                                        "automatic", "Off"};
static FixtureStrId menuFixtureId(const std::string& text) {
  for (size_t index = 0; index < std::size(menuTranslations); ++index) {
    if (text == menuTranslations[index]) return static_cast<FixtureStrId>(index);
  }
  assert(false && "unknown menu translation");
  std::abort();
}

struct MenuSessionBridge;
static MenuSessionBridge* activeMenu;
static XpEpubLeaves menuLeaves;
static XpAppRuntime menuRuntime;
static XpAppResources menuResources;

struct MenuSessionBridge {
  EpubReaderActivity& owner;
  void* rust = nullptr;
  explicit MenuSessionBridge(EpubReaderActivity&);
  ~MenuSessionBridge() {
    assert(activeMenu == this && !lockDepth && !menuModelDepth);
    xp_epub_test_menu_destroy(rust);
    activeMenu = nullptr;
  }
  void observe() {
    XpEpubTestMenuObservation out{};
    xp_epub_test_menu_observe(rust, &out);
    assert(bool(out.has_section) == bool(owner.section));
    assert(bool(out.has_toolbar) == bool(owner.toolbarUi));
    owner.cachedSpineIndex = out.cached_spine;
    owner.cachedChapterTotalPageCount = out.cached_count;
    owner.nextPageNumber = out.next_page;
    owner.currentSpineIndex = out.spine;
    owner.focusedTool = out.focus;
    owner.bookmarkMessageTime = out.bookmark_time;
    owner.pendingManualTurn = out.manual_turn;
    owner.autoTurnOption = out.automatic;
    owner.overlay = static_cast<EpubReaderActivity::Overlay>(out.overlay);
    owner.overlayPageStored = out.stored;
    owner.panelHoldJumped = out.hold;
    owner.panelCursorShown = out.cursor;
    owner.showBookmarkMessage = out.bookmarked;
    owner.pendingScreenshot = out.screenshot;
    assert(out.more_count <= 19);
    owner.moreItems.clear();
    for (uint8_t index = 0; index < out.more_count; ++index)
      owner.moreItems.push_back({static_cast<EpubReaderMenuActivity::MenuAction>(out.more_actions[index]), STR_PORTRAIT});
    xp_epub_test_menu_anchor(rust, [](void* raw, XpAppText text) -> uint8_t {
      static_cast<MenuSessionBridge*>(raw)->owner.pendingAnchor = menuString(text);
      return 1;
    }, this);
  }
  void deliver(const ActivityResult& result) {
    XpAppResultView view{};
    view.original = &result;
    view.cancelled = result.isCancelled;
    if (const auto* menu = std::get_if<MenuResult>(&result.data)) {
      view.kind = XP_APP_RESULT_MENU;
      view.value.menu = {menu->action, menu->orientation, menu->pageTurnOption};
    } else if (const auto* chapter = std::get_if<ChapterResult>(&result.data)) {
      view.kind = XP_APP_RESULT_CHAPTER;
      view.value.chapter = {chapter->spineIndex, menuText(chapter->anchor)};
    } else if (const auto* percent = std::get_if<PercentResult>(&result.data)) {
      view.kind = XP_APP_RESULT_PERCENT;
      view.value.signed_value = percent->percent;
    } else if (const auto* progress = std::get_if<ProgressChangeResult>(&result.data)) {
      view.kind = XP_APP_RESULT_PROGRESS;
      view.value.progress.spine = progress->marker;
    }
    xp_epub_test_menu_result(rust, &view);
    observe();
  }
  void push(void* raw) {
    assert(!menuModelDepth);
    owner.startActivityForResult(std::unique_ptr<Child>(static_cast<Child*>(raw)),
                                [this](const ActivityResult& result) { deliver(result); });
  }
};

static EpubReaderActivity& menuOwner(void* raw) {
  assert(!menuModelDepth && raw == activeMenu);
  return static_cast<MenuSessionBridge*>(raw)->owner;
}

static void configureMenuLeaves() {
  menuLeaves = overlayUnexpectedLeaves();
  menuRuntime = overlayUnexpectedRuntime();
  menuResources = overlayUnexpectedResources();
  menuRuntime.model_lock = [] { assert(!menuModelDepth); ++menuModelDepth; };
  menuRuntime.model_unlock = [] { assert(menuModelDepth == 1); --menuModelDepth; };
  menuRuntime.render_lock = []() -> void* { assert(!menuModelDepth); return new RenderLock; };
  menuRuntime.render_unlock = [](void* raw) { assert(!menuModelDepth); delete static_cast<RenderLock*>(raw); };
  menuRuntime.now_ms = millis;
  menuRuntime.frontlight_state = []() -> uint8_t { return Frontlight.isOn(); };
  menuRuntime.frontlight_set = [](uint8_t on) { Frontlight.setOn(on != 0); };
  menuRuntime.loop_settings = [](XpAppSettings* out) {
    assert(!menuModelDepth);
    *out = {};
    out->reader_orientation = SETTINGS.orientation;
    out->screen_inverted = SETTINGS.screenInverted;
    out->frontlight_on = SETTINGS.frontlightOn;
  };
  menuRuntime.settings_save = [](const XpAppSettings* settings) {
    assert(!menuModelDepth && !lockDepth);
    assert(settings->reader_orientation == SETTINGS.orientation);
    assert(settings->screen_inverted == SETTINGS.screenInverted);
    SETTINGS.frontlightOn = settings->frontlight_on;
    SETTINGS.saveToFile();
  };
  menuResources.translate = [](uint16_t id) -> XpAppText {
    assert(!menuModelDepth);
    for (size_t index = 0; index < std::size(menuKeys); ++index) {
      if (id == static_cast<uint16_t>(menuKeys[index])) return menuLiteral(menuTranslations[index]);
    }
    assert(false && "unexpected menu translation ID");
    std::abort();
  };
  menuLeaves.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    assert(!menuModelDepth && !resolve);
    *out = {};
    out->orientation = SETTINGS.orientation;
    out->screen_inverted = SETTINGS.screenInverted;
  };
  menuLeaves.settings_inversion = [](uint8_t value) { assert(!menuModelDepth); SETTINGS.screenInverted = value; };
  menuLeaves.settings_save = [] { assert(!menuModelDepth); SETTINGS.saveToFile(); };
  menuLeaves.input_services = []() -> const XpUiReaderInputServices* {
    static const XpUiReaderInputServices input{
        nullptr, &OverlayUnexpected<decltype(XpUiReaderInputServices::button)>::call,
        [](void*, uint8_t query) -> uint32_t {
          assert(!menuModelDepth && query == XP_UI_READER_INPUT_HAS_TOUCH);
          return activeMenu->owner.mappedInput.hasTouch();
        }, &OverlayUnexpected<decltype(XpUiReaderInputServices::tap)>::call};
    return &input;
  };
  menuLeaves.section_page_count = [](void* raw) -> uint16_t {
    assert(!menuModelDepth && raw == activeMenu->owner.section.get());
    return static_cast<Section*>(raw)->pageCount;
  };
  menuLeaves.section_estimated_pages = [](void* raw) -> int32_t {
    assert(!menuModelDepth && raw == activeMenu->owner.section.get());
    return static_cast<Section*>(raw)->pageCount;
  };
  menuLeaves.section_building = [](void*) -> uint8_t { assert(!menuModelDepth); return 0; };
  menuLeaves.section_complete = [](void*) -> uint8_t { assert(!menuModelDepth); return 1; };
  menuLeaves.section_partial = [](void*) -> uint8_t { assert(!menuModelDepth); return 0; };
  menuLeaves.section_close = [](void* raw) {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu->owner.section.get());
    activeMenu->owner.section.reset();
  };
  menuLeaves.section_page_text = [](void* raw, int32_t page, XpAppTextSink sink, void* context) -> uint8_t {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu->owner.section.get());
    const auto& section = *static_cast<Section*>(raw);
    assert(page == section.currentPage);
    const auto text = section.getTextFromSectionFile();
    return sink(context, menuText(text));
  };
  menuLeaves.book_title = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    assert(!menuModelDepth && raw == activeMenu->owner.epub.get());
    const auto title = static_cast<Epub*>(raw)->getTitle();
    return sink(context, menuText(title));
  };
  menuLeaves.book_path = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    assert(!menuModelDepth && raw == activeMenu->owner.epub.get());
    const auto path = static_cast<Epub*>(raw)->getPath();
    return sink(context, menuText(path));
  };
  menuLeaves.book_clear_cache = [](void* raw) {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu->owner.epub.get());
    static_cast<Epub*>(raw)->clearCache();
  };
  menuLeaves.book_setup_cache = [](void* raw) {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu->owner.epub.get());
    static_cast<Epub*>(raw)->setupCacheDir();
  };
  menuLeaves.metadata_footnotes = [](void* raw) -> uint16_t {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu);
    return activeMenu->owner.currentPageMetadata.footnotes().size();
  };
  menuLeaves.bookmarks_count = [](void* raw) -> uint32_t {
    assert(!menuModelDepth && raw == &activeMenu->owner.cachedBookmarks);
    return activeMenu->owner.cachedBookmarks.size();
  };
  menuLeaves.clippings_count = []() -> uint32_t { assert(!menuModelDepth); return CLIPPINGS.hasClippings(); };
  menuLeaves.strip_release_idle = [](void* raw) -> uint8_t {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu);
    activeMenu->owner.stripBuffers.releaseIdle();
    return 0;
  };
  menuLeaves.toolbar_open = []() -> void* {
    assert(!menuModelDepth && !lockDepth && !activeMenu->owner.toolbarUi);
    activeMenu->owner.toolbarUi = std::make_unique<ReaderToolbarUi>(activeMenu->owner.renderer);
    return activeMenu->owner.toolbarUi.get();
  };
  menuLeaves.toolbar_begin = [](void* raw) {
    assert(!menuModelDepth && raw == activeMenu->owner.toolbarUi.get());
    static_cast<ReaderToolbarUi*>(raw)->begin();
  };
  menuLeaves.toolbar_close = [](void* raw) {
    assert(!menuModelDepth && lockDepth == 1 && raw == activeMenu->owner.toolbarUi.get());
    activeMenu->owner.toolbarUi.reset();
  };
  menuLeaves.render_restore_bw = [](uint8_t resync) {
    assert(!menuModelDepth && lockDepth == 1);
    activeMenu->owner.renderer.restoreBwBuffer(resync != 0);
  };
  menuLeaves.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    assert(!menuModelDepth && raw == activeMenu);
    static const XpUiReaderRenderServices render{
        nullptr, &OverlayUnexpected<decltype(XpUiReaderRenderServices::read)>::call,
        [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
          assert(!menuModelDepth && lockDepth == 1 && op->kind == XP_UI_READER_RENDER_DISPLAY && op->mode == XP_UI_FAST);
          activeMenu->owner.renderer.displayBuffer(HalDisplay::FAST_REFRESH);
          return 0;
        }};
    return &render;
  };
  menuLeaves.child_menu = [](const XpEpubMenuArgs* args) -> void* {
    assert(!menuModelDepth && !lockDepth);
    return std::make_unique<EpubReaderMenuActivity>(
        activeMenu->owner.renderer, activeMenu->owner.mappedInput, menuString(args->title), args->display_page,
        args->pages, args->percent, args->orientation, args->footnotes != 0, args->bookmarks != 0,
        args->clippings != 0).release();
  };
  menuLeaves.child_chapter = [](void* raw, int32_t toc) -> void* {
    assert(!menuModelDepth && !lockDepth && raw == activeMenu->owner.epub.get());
    return std::make_unique<EpubReaderChapterSelectionActivity>(activeMenu->owner.renderer, activeMenu->owner.mappedInput,
                                                               activeMenu->owner.epub, toc).release();
  };
  menuLeaves.child_text_settings = []() -> void* {
    assert(!menuModelDepth && !lockDepth);
    return std::make_unique<TextSettingsActivity>(activeMenu->owner.renderer, activeMenu->owner.mappedInput,
                                                 &sdFontSystem.registry(), TextSettingsActivity::Tab::Family).release();
  };
  menuLeaves.child_percent = [](int32_t percent) -> void* {
    assert(!menuModelDepth && !lockDepth);
    return std::make_unique<EpubReaderPercentSelectionActivity>(activeMenu->owner.renderer,
                                                               activeMenu->owner.mappedInput, percent).release();
  };
  menuLeaves.child_qr = [](XpAppText text) -> void* {
    assert(!menuModelDepth && !lockDepth);
    return std::make_unique<QrDisplayActivity>(activeMenu->owner.renderer, activeMenu->owner.mappedInput,
                                              menuString(text)).release();
  };
  menuLeaves.child_bookmarks = [](void* raw, XpAppText path) -> void* {
    assert(!menuModelDepth && !lockDepth && raw == activeMenu->owner.epub.get());
    return std::make_unique<EpubReaderBookmarksActivity>(activeMenu->owner.renderer, activeMenu->owner.mappedInput,
                                                        activeMenu->owner.epub, menuString(path)).release();
  };
  menuLeaves.popup_show = [](void* raw, XpAppText title, XpAppText message, uint32_t count, int32_t selected,
                            uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labelContext,
                            void (*choose)(void*, int32_t), void* choiceContext) {
    assert(!menuModelDepth && lockDepth == 1 && raw == &activeMenu->owner.overlayPopup && message.len == 0);
    assert(choiceContext == activeMenu->rust);
    const auto id = menuFixtureId(menuString(title));
    std::vector<std::string> labels;
    labels.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
      struct Label { std::string value; unsigned calls = 0; } value;
      const auto accepted = label(labelContext, index, [](void* context, XpAppText text) -> uint8_t {
        assert(!menuModelDepth && lockDepth == 1);
        auto& value = *static_cast<Label*>(context);
        assert(value.calls++ == 0);
        value.value = menuString(text);
        return 1;
      }, &value);
      assert(accepted && value.calls == 1);
      labels.push_back(std::move(value.value));
    }
    auto callback = [choose, choiceContext](int index) {
      assert(!menuModelDepth && !lockDepth && choiceContext == activeMenu->rust);
      choose(choiceContext, index);
      activeMenu->observe();
    };
    if (id == STR_ORIENTATION) {
      assert(count == 4);
      std::vector<FixtureStrId> ids;
      for (const auto& text : labels) ids.push_back(menuFixtureId(text));
      activeMenu->owner.overlayPopup.show(id, ids.data(), ids.size(), selected, std::move(callback));
    } else {
      assert(id == STR_AUTO_TURN_PAGES_PER_MIN && count == 5);
      activeMenu->owner.overlayPopup.show(id, labels, selected, std::move(callback));
    }
  };
  menuLeaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    assert(!menuModelDepth);
#ifdef MENU_RELEASE_LOG
    if (severity == 0) return;
#endif
    std::printf("%s %s\n", menuString(origin).c_str(), menuString(message).c_str());
  };
}

MenuSessionBridge::MenuSessionBridge(EpubReaderActivity& value) : owner(value) {
  assert(!activeMenu && !lockDepth && !menuModelDepth);
  activeMenu = this;
  configureMenuLeaves();
  XpEpubTestMenuSeed seed{};
  seed.native = this;
  seed.book = owner.epub.get();
  seed.section = owner.section.get();
  seed.toolbar = owner.toolbarUi.get();
  seed.popup = &owner.overlayPopup;
  seed.bookmarks = &owner.cachedBookmarks;
  seed.spine = owner.currentSpineIndex;
  seed.page = owner.section ? owner.section->currentPage : 0;
  seed.cached_spine = owner.cachedSpineIndex;
  seed.cached_count = owner.cachedChapterTotalPageCount;
  seed.next_page = owner.nextPageNumber;
  seed.focus = owner.focusedTool;
  seed.manual_turn = owner.pendingManualTurn;
  seed.automatic = owner.autoTurnOption;
  seed.overlay = static_cast<uint8_t>(owner.overlay);
  seed.stored = owner.overlayPageStored;
  seed.hold = owner.panelHoldJumped;
  seed.cursor = owner.panelCursorShown;
  assert(owner.moreItems.size() <= 19);
  seed.more_count = owner.moreItems.size();
  for (size_t index = 0; index < owner.moreItems.size(); ++index)
    seed.more_actions[index] = static_cast<uint8_t>(owner.moreItems[index].action);
  seed.hooks = {
      this,
      [](void* p, int32_t* out) {
        const auto position = menuOwner(p).chapterPosition();
        out[0] = position.pageIndex; out[1] = position.totalPages;
      },
      [](void* p, int32_t page, int32_t count) -> int32_t {
        return menuOwner(p).bookPercentFor(ChapterPosition{page, count});
      },
      [](void* p, uint8_t value) { menuOwner(p).applyOrientation(value); },
      [](void* p, uint8_t value) { menuOwner(p).toggleAutoPageTurn(value); },
      [](void* p, const XpAppProgressResult* result) {
        menuOwner(p).applyProgressChange(ProgressChangeResult{result->spine});
      },
      [](void* p) { menuOwner(p).clearDeferredReposition(); },
      [](void* p, uint8_t reopen) { menuOwner(p).openFootnoteSelect(reopen != 0); },
      [](void* p, int32_t value) { menuOwner(p).jumpToPercent(value); },
      [](void* p) { menuOwner(p).openDictionaryWordSelect(); },
      [](void* p) { menuOwner(p).onGoHome(); },
      [](void* p, int32_t spine, int32_t page, int32_t count) -> uint8_t {
        return menuOwner(p).saveProgress(spine, page, count);
      },
      [](void* p) { menuOwner(p).launchKOReaderSync(); },
      [](void* p, int32_t x, int32_t y, uint8_t mode) {
        assert(x == -1 && y == -1 && mode == 0);
        menuOwner(p).startClipSelection();
      },
      [](void* p) { menuOwner(p).openClippings(); },
      [](void* p) { menuOwner(p).addBookmark(); },
      [](void*) { assert(!menuModelDepth); EpubReaderMenuActivity::toggleBluetooth(); },
      [](void*, uint8_t footnotes, uint8_t bookmarks, uint8_t clips, uint8_t* out) -> uint8_t {
        assert(!menuModelDepth);
        std::vector<EpubReaderMenuActivity::MenuItem> items;
        EpubReaderMenuActivity::buildMenuItems(items, footnotes != 0, bookmarks != 0, clips != 0);
        assert(items.size() <= 19);
        for (size_t index = 0; index < items.size(); ++index) out[index] = static_cast<uint8_t>(items[index].action);
        return items.size();
      },
      [](void* p) { menuOwner(p).rememberCurrentContentOffset(); },
      [](void* p) { menuOwner(p).paintOverlayPopup(); }};
  seed.toolbar_menu = [](void* p) -> uint8_t { return menuOwner(p).usesToolbarMenu(); };
  seed.toc = [](void* p) -> int32_t { return menuOwner(p).currentTocIndex(); };
  seed.discard = [](void* p) { menuOwner(p).discardOverlayPage(); };
  seed.settle = [](void* p) { menuOwner(p).settleOverlayRefresh(); };
  seed.load_bookmarks = [](void* p) { menuOwner(p).loadCachedBookmarks(); };
  seed.render = [](void* p) { menuOwner(p).renderOverlay(); };
  seed.push_overlay = [](void* p) { menuOwner(p).pushOverlayRefresh(); };
  seed.update = [](void* p) { menuOwner(p).requestUpdate(); };
  seed.push_child = [](void* p, void* raw) { static_cast<MenuSessionBridge*>(p)->push(raw); };
  rust = xp_epub_test_menu_create(&seed, &menuLeaves, &menuRuntime, &menuResources);
  assert(rust);
}

void EpubReaderActivity::openReaderMenu() {
  assert(activeMenu && &activeMenu->owner == this);
  xp_epub_test_menu_open(activeMenu->rust);
  activeMenu->observe();
}
void EpubReaderActivity::onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action) {
  assert(activeMenu && &activeMenu->owner == this);
  xp_epub_test_menu_confirm(activeMenu->rust, static_cast<int32_t>(action));
  activeMenu->observe();
}
void EpubReaderActivity::buildMoreActions() {
  assert(activeMenu && &activeMenu->owner == this);
  xp_epub_test_menu_build_more(activeMenu->rust);
  activeMenu->observe();
}
void EpubReaderActivity::activateMoreRow(int row) {
  assert(activeMenu && &activeMenu->owner == this);
  xp_epub_test_menu_activate(activeMenu->rust, row);
  activeMenu->observe();
}
