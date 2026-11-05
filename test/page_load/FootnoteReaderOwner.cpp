#include <set>
#include <string_view>

#include "FootnoteReader.h"
#include "activities/reader/EpubReaderFootnoteSelectActivity.h"
#include "footnote_app_entry.inc"

struct NavigationObservation {
  int32_t spine, next_page, active_page, cached_count;
  uint32_t cached_offset, pending_offset;
  float spine_progress;
  XpAppText anchor;
  int32_t history[6];
  uint8_t history_depth, has_section, has_cached_offset, has_pending_offset, pending_percent;
};
struct MenuObservation {
  int32_t cached_spine, cached_count, next_page, spine, focus;
  uint32_t bookmark_time;
  int8_t manual_turn;
  uint8_t automatic, overlay, stored, hold, cursor, bookmarked, screenshot, has_section, has_toolbar;
  uint8_t more_actions[19], more_count;
};
extern "C" {
void xp_epub_test_child_footnotes(void*, uint8_t);
void xp_epub_test_loop(void*);
void xp_epub_test_menu_open(void*);
void xp_epub_test_menu_build_more(void*);
void xp_epub_test_menu_observe(void*, MenuObservation*);
void xp_epub_test_navigation_observe(void*, NavigationObservation*);
void* xp_app_test_published_epub();
}

namespace {
Fixture appFixture;
struct NativeSection {
  FootnoteSection* fixture;
};
struct NativeChild {
  std::unique_ptr<Activity> activity;
  bool footnote, entered = false;
};
struct Parent {
  EpubReaderActivity* fixture = nullptr;
  void* owner = nullptr;
  std::unique_ptr<NativeSection> section;
  std::set<NativeChild*> children;
  std::set<Page*> pages;
  Page* bound = nullptr;
  bool availabilityProbe = false, menuFootnotes = false;
  bool returning = false, destroyed = false, unlocked = false, delivered = false;
  NativeChild* returningChild = nullptr;
  unsigned born = 0, dead = 0, returns = 0, setupRenders = 0, renderPasses = 0;
  unsigned menuChildren = 0, selectorChildren = 0;
  int font = 0, left = 0, top = 0;
  void (*captureUnlock)() = nullptr;
} parent;
XpAppText literal(const char* value) { return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)}; }
void physical(int depth = -1) {
  assert(parent.fixture && !f().modelDepth);
  if (depth >= 0) assert(f().renderDepth == depth);
  if (depth == 1) RenderLock::requireHeld();
  if (depth == 0) RenderLock::requireUnlocked();
}
void section(void* raw) {
  physical(1);
  assert(raw == parent.section.get() && parent.section);
}
void book(void* raw) {
  physical();
  f().handle(raw, Kind::Book);
}
void root(void* raw) {
  physical();
  f().handle(raw, Kind::Root);
}

std::unique_ptr<Page> setupPage() {
  uint8_t wire[2048]{};
  XpPageBuilder builder{};
  assert(xp_page_builder_init(&builder, wire, sizeof(wire)) == 0);
  for (unsigned i = 1; i <= 2; ++i) {
    uint8_t number[32]{static_cast<uint8_t>('0' + i)};
    uint8_t href[256]{'#', static_cast<uint8_t>('0' + i)};
    assert(xp_page_builder_footnote(&builder, number, href) == 0);
  }
  for (unsigned i = 2; i > 0; --i) {
    uint8_t href[256]{'#', static_cast<uint8_t>('0' + i)};
    XpPageLink link{{href, sizeof(href)}, static_cast<int16_t>(20 * i), 30, 8, 10};
    assert(xp_page_builder_link(&builder, &link) == 0);
  }
  size_t bytes = 0;
  assert(xp_page_builder_finish(&builder, &bytes) == 0);
  auto page = Page::fromBytes(wire, bytes);
  assert(page);
  return page;
}

NativeChild* currentChild() {
  auto* raw = static_cast<NativeChild*>(xp_app_test_current_legacy());
  assert(parent.children.count(raw));
  return raw;
}
void* child(std::unique_ptr<Activity> value, bool footnote) {
  physical(0);
  assert(value);
  auto* raw = new NativeChild{std::move(value), footnote};
  assert(parent.children.insert(raw).second);
  ++parent.born;
  if (footnote)
    ++parent.selectorChildren;
  else
    ++parent.menuChildren;
  return raw;
}
struct NavigationBefore {
  uint8_t depth;
  std::string anchor;
};
NavigationBefore navigation() {
  NavigationObservation observation{};
  xp_epub_test_navigation_observe(parent.owner, &observation);
  return {observation.history_depth, text(observation.anchor)};
}
void publishNavigation(const NavigationBefore& before) {
  const auto after = navigation();
  if (after.depth != before.depth || after.anchor != before.anchor) {
    assert(!after.anchor.empty());
    parent.fixture->navigatedHref = "#" + after.anchor;
    parent.fixture->isFootnote = after.depth > before.depth;
  }
}
void popChild(const ActivityResult& value) {
  physical(0);
  auto* raw = currentChild();
  // Copy before the real App destroys the child that may own the caller's reference.
  ActivityResult copied = value;
  raw->activity->result = std::move(copied);
  const auto before = navigation();
  XpAppEvent event{};
  event.kind = XP_APP_POP;
  assert(xp_app_event(&event) == XP_APP_ACCEPTED);
  assert(xp_app_test_drain() == 1);
  publishNavigation(before);
}
void drainChild() {
  assert(xp_app_test_drain() == 1);
  assert(xp_app_test_parent_count() == 1 && !xp_app_test_current_epub());
  assert(currentChild()->entered);
}

void bindLifecycle() {
  static auto baseModelLock = runtime.model_lock;
  static auto baseFacts = legacy.facts;
  static auto baseEnter = legacy.enter;
  static auto baseExit = legacy.exit;
  static auto baseDestroy = legacy.destroy;
  static auto baseWithResult = legacy.with_result;
  runtime.model_lock = [] {
    if (parent.returning && parent.destroyed && parent.unlocked && !parent.delivered) {
      assert(xp_app_test_current_epub() == parent.owner && !xp_app_test_current_legacy());
      parent.delivered = true;
    }
    baseModelLock();
  };
  runtime.render_lock = []() -> void* {
    physical(0);
    auto* lock = new RenderLock;
    f().renderDepth = 1;
    return lock;
  };
  runtime.render_try_lock = runtime.render_lock;
  runtime.render_unlock = [](void* raw) {
    physical(1);
    f().renderDepth = 0;
    delete static_cast<RenderLock*>(raw);
    if (parent.returning && parent.destroyed && !parent.unlocked) {
      assert(xp_app_test_current_epub() == parent.owner);
      assert(xp_app_test_published_epub() == parent.owner);
      assert(xp_app_test_parent_count() == 0);
      parent.unlocked = true;
    }
  };
  runtime.notify_render = [] {
    physical();
    ++parent.fixture->updates;
  };
  runtime.touch_feedback = [](uint8_t value) {
    physical(0);
    assert(value <= 1);
  };
  runtime.now_ms = []() -> uint32_t {
    physical();
    return 100;
  };
  runtime.frontlight_present = []() -> uint8_t {
    physical(0);
    return 0;
  };
  legacy.facts = [](const void* raw, uint8_t a, uint8_t b) -> XpAppLegacyFacts {
    if (!parent.children.count(static_cast<NativeChild*>(const_cast<void*>(raw)))) return baseFacts(raw, a, b);
    physical();
    XpAppLegacyFacts facts{};
    facts.kind = UINT8_MAX;
    return facts;
  };
  legacy.enter = [](void* raw) {
    auto* c = static_cast<NativeChild*>(raw);
    if (!parent.children.count(c)) {
      baseEnter(raw);
      return;
    }
    physical(0);
    assert(!c->entered);
    c->entered = true;
    parent.fixture->child = c->activity.get();
    c->activity->onEnter();
  };
  legacy.exit = [](void* raw) {
    auto* c = static_cast<NativeChild*>(raw);
    if (!parent.children.count(c)) {
      baseExit(raw);
      return;
    }
    physical(1);
    assert(c->entered);
  };
  legacy.destroy = [](void* raw) {
    auto* c = static_cast<NativeChild*>(raw);
    if (!parent.children.count(c)) {
      baseDestroy(raw);
      return;
    }
    physical(1);
    assert(parent.children.erase(c) == 1);
    if (parent.fixture->child == c->activity.get()) parent.fixture->child = nullptr;
    delete c;
    ++parent.dead;
    if (parent.returningChild == c) parent.destroyed = true;
  };
  legacy.with_result = [](void* raw, XpAppResultSink sink, void* context) {
    auto* c = static_cast<NativeChild*>(raw);
    if (!parent.children.count(c)) {
      baseWithResult(raw, sink, context);
      return;
    }
    physical(1);
    assert(!parent.returning && sink);
    parent.returning = true;
    parent.returningChild = c;
    parent.destroyed = parent.unlocked = parent.delivered = false;
    ActivityResult owned = std::move(c->activity->result);
    XpAppResultView view{};
    view.original = &owned;
    view.cancelled = owned.isCancelled;
    const auto* note = std::get_if<FootnoteResult>(&owned.data);
    if (note) {
      view.kind = 11;
      view.value.text = {reinterpret_cast<const uint8_t*>(note->href.data()), note->href.size()};
    }
    sink(context, &view);
    // App destroys the dormant child, publishes the parent, unlocks, then delivers.
    assert(parent.destroyed && parent.unlocked && parent.delivered);
    physical(0);
    if (note)
      assert(view.value.text.data == reinterpret_cast<const uint8_t*>(note->href.data()) &&
             view.value.text.len == note->href.size());
    parent.returning = false;
    parent.returningChild = nullptr;
    ++parent.returns;
  };
}

const XpUiReaderRenderServices renderServices{nullptr,
                                              [](void*, uint8_t query) -> uint32_t {
                                                physical(1);
                                                switch (query) {
                                                  case XP_UI_READER_RENDER_NOW:
                                                    return 100;
                                                  case XP_UI_READER_RENDER_FREQUENCY:
                                                    return 3;
                                                  case XP_UI_READER_RENDER_FREE_HEAP:
                                                  case XP_UI_READER_RENDER_MAX_BLOCK:
                                                    return 100000;
                                                  case XP_UI_READER_RENDER_HEIGHT:
                                                    return 800;
                                                  case XP_UI_READER_RENDER_WIDTH_BYTES:
                                                    return 60;
                                                  case XP_UI_READER_RENDER_IS_BW:
                                                    return 1;
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
                                                  case XP_UI_READER_RENDER_ELEMENTS:
                                                    return 0;
                                                  default:
                                                    assert(false && "unexpected footnote renderer query");
                                                    return 0;
                                                }
                                              },
                                              [](void*, const XpUiReaderRenderOp* op) -> uint32_t {
                                                physical(1);
                                                assert(op);
                                                switch (op->kind) {
                                                  case XP_UI_READER_RENDER_PAGE:
                                                    assert(parent.bound);
                                                    ++parent.renderPasses;
                                                    parent.bound->render(parent.fixture->renderer, parent.font,
                                                                         parent.left, parent.top);
                                                    return 1;
                                                  case XP_UI_READER_RENDER_STORE_BW:
                                                  case XP_UI_READER_RENDER_CLEANUP:
                                                  case XP_UI_READER_RENDER_DISPLAY_ASYNC:
                                                  case XP_UI_READER_RENDER_DISPLAY:
                                                    return 1;
                                                  case XP_UI_READER_RENDER_CLEAR:
                                                  case XP_UI_READER_RENDER_IMAGE_RESET:
                                                  case XP_UI_READER_RENDER_PREWARM_BEGIN:
                                                  case XP_UI_READER_RENDER_PREWARM_END:
                                                  case XP_UI_READER_RENDER_MODE:
                                                  case XP_UI_READER_RENDER_MARK:
                                                  case XP_UI_READER_RENDER_LOG:
                                                  case XP_UI_READER_RENDER_RELEASE_STRIPS:
                                                    return 0;
                                                  default:
                                                    assert(false && "unexpected footnote renderer operation");
                                                    return 0;
                                                }
                                              }};

void bindReader() {
  leaves.settings_snapshot = [](uint8_t resolve, XpEpubSettings* out) {
    physical();
    assert(resolve <= 1 && out);
    *out = {};
    out->font = resolve ? 14 : 0;
    out->embedded_style = 1;
    out->touch_controls = 1;
    out->page_gesture = out->previous_gesture = 2;  // Physical settings: swipe-only page turns.
  };
  leaves.render_geometry = [](XpEpubGeometry* out) {
    physical();
    *out = {};
    out->width = 480;
    out->height = 800;
    out->has_framebuffer = 1;
    out->title_mode = 2;
    out->ui_font_12 = 14;
  };
  leaves.render_margins = [](int32_t* out) {
    physical();
    out[0] = parent.fixture->currentPageLinkMarginTop;
    out[1] = out[2] = 0;
    out[3] = parent.fixture->currentPageLinkMarginLeft;
  };
  leaves.settings_render_spec = [](uint16_t width, uint16_t height, XpEpubRenderSpec* out) {
    physical();
    assert(width == 475 && height == 793);
    *out = {};
    out->font = 14;
    out->compression = 1;
    out->width = width;
    out->height = height;
    out->word_spacing_percent = 100;
  };
  legacyBook.section.page.render_services = [](void* raw) -> const XpUiReaderRenderServices* {
    physical(1);
    root(raw);
    return &renderServices;
  };
  legacyBook.section.page.render_bind_page = [](void* raw, void* page, int32_t font, int32_t left, int32_t top, int32_t,
                                                XpEpubLegacyHighlight, void*) {
    physical(1);
    root(raw);
    assert(parent.pages.count(static_cast<Page*>(page)) && !parent.bound);
    parent.bound = static_cast<Page*>(page);
    parent.font = font;
    parent.left = left;
    parent.top = top;
  };
  legacyBook.section.page.render_finish_page = [](void* raw) {
    physical(1);
    root(raw);
    assert(parent.bound);
    parent.bound = nullptr;
  };
  leaves.render_busy = []() -> uint8_t {
    physical();
    return 0;
  };
  legacyBook.section.page.strip_release_idle = [](void* raw) -> uint8_t {
    physical(1);
    root(raw);
    return 1;
  };
  legacyBook.book_spine_count = [](void* raw) -> int32_t {
    book(raw);
    return 2;
  };
  legacyBook.book_size = [](void* raw) -> uint32_t {
    book(raw);
    return 1000;
  };
  legacyBook.book_progress = [](void* raw, int32_t spine, float) -> float {
    book(raw);
    assert(spine == 1);
    return .5f;
  };
  legacyBook.book_spine = [](void* raw, int32_t index, XpEpubSpineSink sink, void* context) -> uint8_t {
    book(raw);
    assert(index == 1);
    const XpEpubSpine item{literal("chapter.xhtml"), 100};
    return sink(context, &item);
  };
  legacyBook.book_title = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    book(raw);
    return deliver(sink, context, "Fixture");
  };
  legacyBook.book_language = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    book(raw);
    return deliver(sink, context, "en");
  };
  legacyBook.book_path = [](void* raw, XpAppTextSink sink, void* context) -> uint8_t {
    book(raw);
    return deliver(sink, context, f().path.c_str());
  };
  legacyBook.section.section_open = [](void* raw, int32_t spine) -> void* {
    physical(1);
    book(raw);
    assert(spine == 1 && !parent.section);
    parent.section = std::make_unique<NativeSection>(NativeSection{parent.fixture->section});
    return parent.section.get();
  };
  legacyBook.section.section_close = [](void* raw) {
    section(raw);
    assert(!parent.bound);
    parent.section.reset();
  };
  legacyBook.section.section_page_count = [](void* raw) -> uint16_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_estimated_pages = [](void* raw) -> int32_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_building = [](void* raw) -> uint8_t {
    section(raw);
    return 0;
  };
  legacyBook.section.section_complete = [](void* raw) -> uint8_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_partial = [](void* raw) -> uint8_t {
    section(raw);
    return 0;
  };
  legacyBook.section.section_load_cache = [](void* raw, const XpEpubRenderSpec*) -> uint8_t {
    section(raw);
    return 1;
  };
  legacyBook.section.section_find_anchor = [](void* raw, XpAppText anchor, uint16_t* page) -> uint8_t {
    section(raw);
    assert(text(anchor) == "1" || text(anchor) == "2");
    *page = 0;
    return 1;
  };
  legacyBook.section.section_load_page = [](void* raw, int32_t index) -> void* {
    section(raw);
    assert(index == 0);
    auto page = parent.section->fixture->loadPage(index);
    assert(page);
    assert(parent.pages.insert(page.get()).second);
    return page.release();
  };
  legacyBook.section.page.page_close = [](void* raw) {
    physical(1);
    auto* page = static_cast<Page*>(raw);
    assert(parent.pages.erase(page) == 1 && parent.bound != page);
    delete page;
  };
  legacyBook.section.page.page_info = [](void* raw, XpEpubPageInfo* out) {
    physical(1);
    auto* page = static_cast<Page*>(raw);
    assert(parent.pages.count(page));
    *out = {};
    out->elements = page->elementCount();
    out->footnotes = page->footnoteCount();
    out->links = page->linkCount();
  };
  legacyBook.section.page.metadata_clear = [](void* raw) {
    physical(1);
    root(raw);
    parent.fixture->currentPageMetadata.clear();
  };
  legacyBook.section.page.metadata_assign = [](void* raw, void* page) -> uint8_t {
    physical(1);
    root(raw);
    assert(parent.pages.count(static_cast<Page*>(page)));
    return parent.fixture->currentPageMetadata.assign(*static_cast<Page*>(page));
  };
  legacyBook.section.page.metadata_footnotes = [](void* raw) -> uint16_t {
    physical(1);
    root(raw);
    return parent.fixture->currentPageMetadata.footnotes().size();
  };
  legacyBook.section.page.metadata_footnote = [](void* raw, uint16_t index, XpEpubFootnoteSink sink,
                                                 void* context) -> uint8_t {
    physical(1);
    root(raw);
    const auto rows = parent.fixture->currentPageMetadata.footnotes();
    assert(index < rows.size());
    const XpEpubFootnote item{literal(rows[index].number), literal(rows[index].href)};
    return sink(context, &item);
  };
  legacyBook.section.page.metadata_links = [](void* raw) -> uint16_t {
    physical(1);
    root(raw);
    return parent.fixture->currentPageMetadata.links().size();
  };
  legacyBook.section.page.metadata_link = [](void* raw, uint16_t index, XpEpubLinkSink sink, void* context) -> uint8_t {
    physical(1);
    root(raw);
    const auto rows = parent.fixture->currentPageMetadata.links();
    assert(index < rows.size());
    if (parent.captureUnlock) {
      assert(!RenderLock::afterUnlock);
      RenderLock::afterUnlock = parent.captureUnlock;
      parent.captureUnlock = nullptr;
    }
    const auto& row = rows[index];
    const XpEpubLink item{literal(row.href), row.x, row.y, row.width, row.height};
    return sink(context, &item);
  };
  legacyBook.section.page.child_footnote = [](void* raw, int32_t left, int32_t top) -> void* {
    physical(0);
    auto* page = static_cast<Page*>(raw);
    assert(parent.pages.erase(page) == 1);
    return child(std::make_unique<EpubReaderFootnoteSelectActivity>(
                     parent.fixture->renderer, parent.fixture->mappedInput, std::unique_ptr<Page>(page), left, top),
                 true);
  };
  leaves.child_menu = [](const XpEpubMenuArgs* args) -> void* {
    physical(0);
    assert(args && text(args->title) == "Fixture");
    parent.menuFootnotes = args->footnotes != 0;
    if (!parent.availabilityProbe) ++parent.fixture->menuRequests;
    return child(std::make_unique<Activity>("Menu", parent.fixture->renderer, parent.fixture->mappedInput), false);
  };
  leaves.bookmarks_count = [](void* raw) -> uint32_t {
    physical();
    f().handle(raw, Kind::Bookmarks);
    return 0;
  };
  leaves.clippings_count = []() -> uint32_t {
    physical();
    return 0;
  };
  leaves.progress_save = [](void* raw, const XpEpubPosition*) -> uint8_t {
    book(raw);
    return 1;
  };
  leaves.paint_status = [](const XpEpubStatus* status) {
    physical(1);
    assert(status && status->pages == 1);
  };
  leaves.radio_status = [](XpEpubRadio* out, XpEpubPeer* peer) {
    physical();
    assert(out && !peer);
    *out = {};
  };
  leaves.radio_settle_start = [] { physical(1); };
  leaves.radio_after_paint = [] { physical(1); };
  leaves.fonts_take_corrupt_notice = []() -> uint8_t {
    physical(1);
    return 0;
  };
  leaves.trusted_now = []() -> int64_t {
    physical();
    return 1000;
  };
  leaves.popup_active = [](void* raw) -> uint8_t {
    physical();
    const auto found = f().handles.find(raw);
    assert(found != f().handles.end());
    return 0;
  };
  leaves.state_set_open_path = [](XpAppText path) {
    physical(0);
    assert(text(path) == f().path);
  };
  static auto baseStateSave = leaves.state_save;
  leaves.state_save = [] {
    if (f().renderDepth)
      baseStateSave();
    else
      physical(0);
  };
  legacyBook.recents_add = [](void* raw) {
    physical(0);
    book(raw);
  };
  leaves.emit_open = [](XpAppText path) {
    physical(0);
    assert(text(path) == f().path);
  };
  leaves.file_open_write = [](XpAppText path, XpAppText origin) -> void* {
    assert(origin.len == 3 && std::memcmp(origin.data, "ERS", 3) == 0);
    physical(1);
    assert(text(path) == "/cache/links.bin");
    return f().acquire(Kind::File, f().generation);
  };
  leaves.file_write = [](void* raw, const uint8_t* bytes, uint32_t count) -> uint32_t {
    physical(1);
    f().handle(raw, Kind::File);
    assert(bytes && count > 0);
    return count;
  };
  static auto entryLog = leaves.log;
  leaves.log = [](uint8_t severity, XpAppText origin, XpAppText message) {
    if (text(origin) == "ERS" && text(message) != "Loaded cache: 1, 2") {
      physical();
      return;
    }
    entryLog(severity, origin, message);
  };
}

MappedInputManager::Button nativeButton(uint32_t mask) {
  using Button = MappedInputManager::Button;
  switch (mask) {
    case XP_UI_BACK:
      return Button::Back;
    case XP_UI_CONFIRM:
      return Button::Confirm;
    case XP_UI_POWER:
      return Button::Power;
    case XP_UI_LEFT:
    case XP_UI_SCREEN_LEFT:
      return Button::ScreenLeft;
    case XP_UI_RIGHT:
    case XP_UI_SCREEN_RIGHT:
      return Button::ScreenRight;
    case XP_UI_UP:
    case XP_UI_SCREEN_UP:
      return Button::ScreenUp;
    case XP_UI_DOWN:
    case XP_UI_SCREEN_DOWN:
      return Button::ScreenDown;
    case XP_UI_PAGE_BACK:
    case XP_UI_PAGE_FORWARD:
      return Button::None;
    default:
      assert(false && "unknown shared button mask");
      return Button::None;
  }
}
const XpUiReaderInputServices inputServices{
    nullptr,
    [](void*, uint32_t mask, uint8_t query, uint32_t) -> uint8_t {
      physical(0);
      const auto button = nativeButton(mask);
      if (button == MappedInputManager::Button::None) return 0;
      const auto& input = parent.fixture->mappedInput;
      switch (query) {
        case XP_UI_READER_BUTTON_PRESS:
          return input.wasPressed(button);
        case XP_UI_READER_BUTTON_RELEASE:
          return input.wasReleased(button);
        case XP_UI_READER_BUTTON_LONG:
          return 0;  // The physical fixture has no held keys.
        default:
          assert(false && "unknown mapped button query");
          return 0;
      }
    },
    [](void*, uint8_t query) -> uint32_t {
      physical(0);
      switch (query) {
        case XP_UI_READER_INPUT_HAS_TOUCH:
          return parent.fixture->mappedInput.hasTouch();
        case XP_UI_READER_INPUT_HELD_MS:
        case XP_UI_READER_INPUT_TOUCH_HELD_MS:
          return parent.fixture->mappedInput.getHeldTime();
        case XP_UI_READER_INPUT_SWIPE:  // Shared zero is no swipe; no native enum cast.
        case XP_UI_READER_INPUT_TILT_NEXT:
        case XP_UI_READER_INPUT_TILT_PREV:
        case XP_UI_READER_INPUT_NAV_SWAPPED:
        case XP_UI_READER_INPUT_HOME_ACTION:
        case XP_UI_READER_INPUT_BACK_GESTURE:
        case XP_UI_READER_INPUT_MENU_GESTURE:
        case XP_UI_READER_INPUT_MENU_SWIPE_UP:
          return 0;
        default:
          assert(false && "unknown mapped input query");
          return 0;
      }
    },
    [](void*, int32_t* x, int32_t* y) -> uint8_t {
      physical(0);
      return parent.fixture->mappedInput.wasScreenTapped(*x, *y);
    }};
void bindInput() {
  leaves.input_services = []() -> const XpUiReaderInputServices* {
    physical(0);
    return &inputServices;
  };
  leaves.input_facts = [](XpEpubInputFacts* out) {
    physical(0);
    assert(out);
    *out = {};
    out->touch = parent.fixture->mappedInput.hasTouch();
    out->board_touch = 1;
  };
  leaves.input_screen_tap = [](int32_t* x, int32_t* y) -> uint8_t {
    physical(0);
    return parent.fixture->mappedInput.wasScreenTapped(*x, *y);
  };
  leaves.input_screen_long = [](int32_t* x, int32_t* y) -> uint8_t {
    physical(0);
    return parent.fixture->mappedInput.wasScreenLongPress(*x, *y);
  };
}
}  // namespace

EpubReaderActivity::EpubReaderActivity(FootnoteSection& s, GfxRenderer& r, MappedInputManager& m)
    : section(&s), renderer(r), mappedInput(m) {
  assert(!parent.fixture);
  parent.fixture = this;
  fixture = &appFixture;
  bind();
  bindLifecycle();
  bindReader();
  bindInput();
  initialize();
  f().startAttempt();
  assert(install() == 1);
  parent.owner = xp_app_test_current_epub();
  assert(parent.owner);
  // Earlier standalone selector frames have already been consumed.
  mappedInput.pressed = mappedInput.released = MappedInputManager::Button::None;
  completion = [](const ActivityResult& value) { popChild(value); };
  preparePage();
}

void EpubReaderActivity::preparePage() {
  physical(0);
  assert(parent.fixture == this && xp_app_test_current_epub() == parent.owner);
  assert(!parent.section && !section->loaded && !RenderLock::afterUnlock);
  const int measuredLoads = section->loads;
  const unsigned before = parent.renderPasses;
  section->loaded = setupPage();
  void* lock = runtime.render_lock();
  XpAppRenderToken token{lock};
  xp_app_render(&token);
  runtime.render_unlock(lock);
  assert(parent.section && !parent.bound && parent.pages.empty());
  assert(parent.renderPasses > before && section->loads == measuredLoads + 1);
  // Only legitimate unmeasured setup loads are removed from the old selector counter.
  section->loads = measuredLoads;
  ++parent.setupRenders;
}

void EpubReaderActivity::openFootnoteSelect(bool reopenMenuOnCancel) {
  physical(0);
  assert(xp_app_test_current_epub() == parent.owner);
  const auto before = navigation();
  xp_epub_test_child_footnotes(parent.owner, reopenMenuOnCancel);
  publishNavigation(before);
  if (xp_app_test_pending_kind() != 0) drainChild();
}

bool EpubReaderActivity::testMenuAvailability() {
  physical(0);
  assert(xp_app_test_current_epub() == parent.owner);
  parent.availabilityProbe = true;
  parent.menuFootnotes = false;
  xp_epub_test_menu_open(parent.owner);
  drainChild();
  assert(!currentChild()->footnote);
  const bool available = parent.menuFootnotes;
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  popChild(cancelled);
  assert(xp_app_test_current_epub() == parent.owner && !child);
  parent.availabilityProbe = false;
  return available;
}

bool EpubReaderActivity::testMoreAvailability() {
  physical(0);
  assert(xp_app_test_current_epub() == parent.owner);
  xp_epub_test_menu_build_more(parent.owner);
  MenuObservation observation{};
  xp_epub_test_menu_observe(parent.owner, &observation);
  assert(observation.more_count <= 19);
  // Action::Footnotes is 1 in the existing reader menu action enum.
  return std::find(observation.more_actions, observation.more_actions + observation.more_count, uint8_t{1}) !=
         observation.more_actions + observation.more_count;
}

void EpubReaderActivity::testLinkTap() {
  physical(0);
  assert(xp_app_test_current_epub() == parent.owner && !parent.captureUnlock);
  const auto before = navigation();
  // Maintenance precedes link capture in the complete loop; arm only its metadata scope.
  parent.captureUnlock = RenderLock::afterUnlock;
  RenderLock::afterUnlock = nullptr;
  xp_epub_test_loop(parent.owner);
  assert(!parent.captureUnlock && !RenderLock::afterUnlock);
  publishNavigation(before);
}

EpubReaderActivity::~EpubReaderActivity() {
  physical(0);
  assert(!parent.captureUnlock && !RenderLock::afterUnlock);
  if (xp_app_test_current_legacy()) {
    assert(!currentChild()->footnote);
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    popChild(cancelled);
  }
  assert(xp_app_test_current_epub() == parent.owner && parent.children.empty());
  XpAppEvent event{};
  event.kind = XP_APP_SLEEP_REQUESTED;
  event.value.sleep_from_timeout = 0;
  assert(xp_app_event(&event) == XP_APP_ACCEPTED);
  assert(!xp_app_test_current_epub() && xp_app_test_current_legacy());
  assert(!parent.section && parent.pages.empty() && !parent.bound && !child);
  assert(parent.born == parent.dead && parent.returns == parent.dead && parent.setupRenders == 3);
  assert(parent.menuChildren == 3 && parent.selectorChildren == 2 && parent.born == 5);
  assert(!f().extractor && !f().clippings && f().handles.size() == 1);
  parent.owner = nullptr;
  parent.fixture = nullptr;
}
