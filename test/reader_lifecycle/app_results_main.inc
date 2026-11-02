// The driver extracts the native borrowed-result producer into this App fixture.
#include <xp_ui_reader.h>
#include "ActivityResult.h"
#include "ActivityResultView.h"
#include "menu_session_fixture.h"
#include <set>

extern "C" uint8_t xp_epub_test_has_child(const void*);
extern "C" void xp_epub_test_child_footnotes(void*, uint8_t);
extern "C" void xp_epub_test_child_selection(void*, int32_t, int32_t, uint8_t);
extern "C" void xp_epub_test_recovery_begin(void*);
extern "C" void* xp_app_test_published_epub();
extern "C" void xp_epub_test_text_settings_open(void*, uint8_t);
extern "C" void xp_epub_test_overlay_close(void*);

namespace {
XpAppText literal(const char* value) { return {reinterpret_cast<const uint8_t*>(value), std::strlen(value)}; }
struct ResultPage { int page; };
struct ResultSection { int spine; };
struct ResultToolbar { unsigned begins = 0; };
struct Activity {
  ActivityResult result;
  ActivityResultHandler resultHandler;
  std::string kind;
  std::vector<ResultPage*> pages;
  bool entered = false;
  virtual ~Activity();
};
// OWNED_NATIVE_DECLARATION
// NATIVE_ACTIVITY_GETTER

struct Results {
  void* owner = nullptr;
  std::set<void*> children;
  std::set<ResultPage*> pages;
  std::vector<uint8_t> savedLinks;
  ResultSection* section = nullptr;
  ResultToolbar* toolbar = nullptr;
  bool toolbarMode = false;
  unsigned toolbarBegins = 0, settingsSaved = 0;
  void* bound = nullptr;
  void* returning = nullptr;
  bool inResult = false, destroyed = false, unlocked = false, delivered = false;
  bool notificationPending = false, sleepPainted = false, deferParentPop = false;
  void (*popupChoice)(void*, int32_t) = nullptr;
  void* popupContext = nullptr;
  void* popupHandle = nullptr;
  const uint8_t* movedText = nullptr;
  unsigned born = 0, dead = 0, returns = 0, inserts = 0, exports = 0, lookups = 0;
  unsigned bookmarkWrites = 0, wifiChecks = 0, syncs = 0;
  unsigned rendered = 0, sectionOpened = 0, sectionClosed = 0;
  unsigned firstDeliveryLocks = 0, borrowedChecks = 0, beforeUnlockLocks = 0;
  size_t deliveryBegin = 0;
  const ActivityResult* borrowed = nullptr;
  const uint8_t* borrowedText = nullptr;
  std::string expectedKind;
  bool cancelled = false, allocatingStack = false;
} results;

void requireResult(bool condition, const char* marker) {
  if (!condition) { std::fprintf(stderr, "EXPECTED_ASSERTION: %s\n", marker); std::exit(42); }
}
void physical(const char* name, int depth = -1) {
  assert(f().modelDepth == 0);
  if (depth >= 0) assert(f().renderDepth == depth);
  f().physical(name);
}
void releasePage(ResultPage* page) {
  assert(page && results.pages.erase(page) == 1 && results.bound != page);
  physical("page-close");
  delete page;
}
Activity::~Activity() {
  physical("child-destructor", entered ? 1 : -1);
  for (auto* page : pages) releasePage(page);
  ++results.dead;
}
void* child(const char* kind, std::vector<ResultPage*> pages = {}) {
  physical("child-construct", 0);
  auto activity = std::make_unique<Activity>();
  activity->kind = kind;
  activity->pages = std::move(pages);
  auto* owned = new OwnedLegacyActivity(std::move(activity));
  assert(results.children.insert(owned).second);
  ++results.born;
  return owned;
}
void borrowedUse(const char* event) {
  assert(results.inResult && results.destroyed && results.unlocked && results.delivered);
  assert(results.borrowed && xp_app_test_current_epub() == results.owner);
  assert(!xp_app_test_current_legacy());
  physical(event);
}
void checkBorrowed(const XpAppResultView& view, const ActivityResult& original) {
  assert(view.original == &original && view.kind == original.data.index());
  assert(view.cancelled == original.isCancelled);
  const auto same = [](XpAppText wire, const std::string& native) {
    assert(wire.len == native.size() && wire.data == reinterpret_cast<const uint8_t*>(native.data()));
    assert(std::memcmp(wire.data, native.data(), wire.len) == 0);
  };
  switch (view.kind) {
    case 0: break;
    case 1: {
      const auto& v = std::get<WifiResult>(original.data);
      same(view.value.wifi.ssid, v.ssid); same(view.value.wifi.ip, v.ip);
      assert(view.value.wifi.connected == v.connected); break;
    }
    case 3: {
      const auto& v = std::get<MenuResult>(original.data);
      assert(view.value.menu.action == v.action && view.value.menu.orientation == v.orientation &&
             view.value.menu.page_turn_option == v.pageTurnOption); break;
    }
    case 4: {
      const auto& v = std::get<ChapterResult>(original.data);
      same(view.value.chapter.anchor, v.anchor); assert(view.value.chapter.spine == v.spineIndex);
      assert(view.value.chapter.anchor.data == results.movedText); break;
    }
    case 5: assert(view.value.signed_value == std::get<PercentResult>(original.data).percent); break;
    case 8: {
      const auto& v = std::get<ClippingResult>(original.data); const auto& c = view.value.clipping;
      same(c.text.bytes, v.text); assert(c.text.native_string == &v.text);
      assert(c.text.bytes.data == results.movedText);
      assert(c.action == uint8_t(v.action) && c.start_page_offset == v.startPageOffset &&
             c.end_page_offset == v.endPageOffset && c.start_word == v.startWordIndex &&
             c.end_word == v.endWordIndex && c.word_count == v.wordCount &&
             c.start_offset == v.startOffset && c.end_offset == v.endOffset);
      results.borrowedText = c.text.bytes.data; break;
    }
    case 9: {
      const auto& v = std::get<ProgressChangeResult>(original.data); const auto& p = view.value.progress;
      same(p.xpath, v.xpath);
      assert(p.spine == v.spineIndex && p.page == v.page && p.pages == v.totalPages &&
             p.percentage == v.percentage && p.has_saved_progress == v.hasSavedProgress &&
             p.has_offset == v.hasVisibleTextOffset && p.offset == v.visibleTextOffset); break;
    }
    case 11: same(view.value.text, std::get<FootnoteResult>(original.data).href); break;
    default: assert(false && "variant not consumed by EPUB");
  }
  ++results.borrowedChecks;
}

static const auto nativeWithResult = + // NATIVE_WITH_RESULT
;

void bindResultsLifecycle() {
  static auto baseFacts = legacy.facts;
  static auto baseEnter = legacy.enter;
  static auto baseExit = legacy.exit;
  static auto baseDestroy = legacy.destroy;
  static auto baseWithResult = legacy.with_result;
  static auto baseModelLock = runtime.model_lock;
  static auto baseUnlock = runtime.render_unlock;
  static auto baseBefore = runtime.ble_before_screen_change;
  runtime.ble_before_screen_change = [](uint8_t reader) -> uint8_t {
    if (results.deferParentPop && results.returns && xp_app_test_current_epub() == results.owner) {
      physical("radio-defer-parent-pop", 0); return 0;
    }
    return baseBefore(reader);
  };
  legacy.facts = [](const void* raw, uint8_t a, uint8_t b) -> XpAppLegacyFacts {
    if (!results.children.count(const_cast<void*>(raw))) return baseFacts(raw, a, b);
    physical("child-facts"); XpAppLegacyFacts facts{}; facts.kind = UINT8_MAX; return facts;
  };
  legacy.enter = [](void* raw) {
    if (!results.children.count(raw)) { baseEnter(raw); return; }
    physical("child-enter", 0); auto* a = nativeActivity(raw); assert(!a->entered); a->entered = true;
  };
  legacy.exit = [](void* raw) {
    if (!results.children.count(raw)) { baseExit(raw); return; }
    physical("child-exit", 1); assert(nativeActivity(raw)->entered);
  };
  legacy.destroy = [](void* raw) {
    if (!results.children.count(raw)) { baseDestroy(raw); return; }
    assert(results.children.erase(raw) == 1);
    delete static_cast<OwnedLegacyActivity*>(raw);
    if (raw == results.returning) { assert(results.inResult); results.destroyed = true; }
  };
  legacy.prepare_sleep = [](void* raw) {
    physical("child-prepare-sleep", 0); assert(results.children.count(raw));
  };
  legacy.render = [](void* raw, XpAppRenderToken* token) {
    physical("sleep-render", 1); assert(token && token->lock);
    auto& sleep = f().handle(raw, Kind::Legacy); assert(sleep.activityKind == XP_APP_SLEEP);
    assert(!results.section && results.pages.empty() && !xp_app_test_current_epub());
    results.sleepPainted = true;
  };
  runtime.model_lock = [] {
    if (results.inResult && results.destroyed && !results.unlocked && f().renderDepth == 1) {
      requireResult(++results.beforeUnlockLocks == 1, "APP_RESULT_BEFORE_UNLOCK");
    }
    if (results.inResult && results.destroyed && results.unlocked && !results.delivered) {
      assert(xp_app_test_current_epub() == results.owner && !xp_app_test_current_legacy());
      results.delivered = true; ++results.firstDeliveryLocks;
      physical("parent-result-model-entry", 0);
    }
    baseModelLock();
  };
  runtime.render_unlock = [](void* token) {
    const bool returning = results.inResult && results.destroyed && !results.unlocked;
    if (returning) {
      assert(xp_app_test_current_epub() == results.owner && !xp_app_test_current_legacy());
      assert(xp_app_test_parent_count() == 0);
    }
    baseUnlock(token);
    if (returning) {
      // Observe the actual render publication after physical unlock. This probe takes ModelLock.
      requireResult(xp_app_test_published_epub() == results.owner, "APP_RESULT_NOT_PUBLISHED");
      physical("parent-publication-observed", 0);
      results.unlocked = true;
    }
  };
  legacy.with_result = [](void* raw, XpAppResultSink sink, void* context) {
    if (!results.children.count(raw)) { baseWithResult(raw, sink, context); return; }
    assert(!results.inResult && f().renderDepth == 1 && xp_app_test_current_legacy() == raw);
    results.inResult = true; results.returning = raw;
    results.destroyed = results.unlocked = results.delivered = false;
    results.beforeUnlockLocks = 0;
    results.deliveryBegin = f().events.size(); physical("borrowed-result-begin", 1);
    results.movedText = nullptr;
    if (const auto* clip = std::get_if<ClippingResult>(&nativeActivity(raw)->result.data))
      results.movedText = reinterpret_cast<const uint8_t*>(clip->text.data());
    if (const auto* chapter = std::get_if<ChapterResult>(&nativeActivity(raw)->result.data))
      results.movedText = reinterpret_cast<const uint8_t*>(chapter->anchor.data());
    struct Context { XpAppResultSink sink; void* context; } checked{sink, context};
    nativeWithResult(raw, [](void* opaque, const XpAppResultView* view) {
      auto& checked = *static_cast<Context*>(opaque);
      assert(view && view->original);
      results.borrowed = static_cast<const ActivityResult*>(view->original);
      checkBorrowed(*view, *results.borrowed);
      checked.sink(checked.context, view);
      assert(results.destroyed && results.unlocked && results.delivered);
      assert(!f().renderDepth && !f().modelDepth && xp_app_test_current_epub() == results.owner);
      // The native automatic result still owns these exact bytes after parent delivery.
      checkBorrowed(*view, *results.borrowed);
      physical("borrowed-result-end", 0);
      results.borrowed = nullptr;
    }, &checked);
    const size_t begin = results.deliveryBegin;
    const auto destroyed = f().find("child-destructor", 0, begin);
    const auto header = f().find("header-clear", 0, destroyed + 1);
    const auto publishLock = f().find("model-lock", 0, header + 1);
    const auto publishUnlock = f().find("model-unlock", 0, publishLock + 1);
    const auto unlocked = f().find("render-unlock", 0, publishUnlock + 1);
    const auto observed = f().find("parent-publication-observed", 0, unlocked + 1);
    const auto delivered = f().find("parent-result-model-entry", 0, observed + 1);
    assert(begin < destroyed && destroyed < header && header + 1 == publishLock);
    assert(publishLock + 1 == publishUnlock && publishUnlock + 1 == unlocked);
    assert(unlocked < observed && observed < delivered);
    results.inResult = false; results.returning = nullptr; ++results.returns;
  };
  runtime.notify_render = [] { physical("notify-render"); results.notificationPending = true; };
  runtime.diagnostic = [](uint8_t code, uint32_t a, uint32_t b) {
    assert(results.allocatingStack && code == 2 && a == 1 && b == 0);
    physical("stack-reserve-failed", 1);
  };
}

// PHYSICAL_BINDINGS

void paintOwner() {
  assert(xp_app_test_current_epub() == results.owner);
  void* lock = runtime.render_lock(); XpAppRenderToken token{lock};
  xp_app_render(&token); runtime.render_unlock(lock);
  assert(results.section && results.rendered > 0 && !results.bound);
}
void menuDismiss() {
  assert(xp_app_test_current_legacy());
  auto* a = nativeActivity(xp_app_test_current_legacy()); assert(a->kind == "menu");
  a->result = MenuResult{-1, 0, 0};
  XpAppEvent pop{}; pop.kind = XP_APP_POP; assert(xp_app_event(&pop) == XP_APP_ACCEPTED);
  assert(xp_app_test_drain() == 1);
  assert(xp_app_test_current_epub() == results.owner);
}
void queuePop(ActivityResult value, bool cancelled) {
  assert(xp_app_test_current_legacy());
  auto* a = nativeActivity(xp_app_test_current_legacy());
  a->result = std::move(value); a->result.isCancelled = cancelled;
  XpAppEvent pop{}; pop.kind = XP_APP_POP; assert(xp_app_event(&pop) == XP_APP_ACCEPTED);
  const auto drained = xp_app_test_drain();
  assert(drained == (results.deferParentPop ? 0 : 1));
  if (results.deferParentPop) assert(xp_app_test_pending_kind() == 2);
}
ActivityResult payload(const std::string& mode) {
  if (mode == "menu") return MenuResult{13, 0, 0};
  if (mode == "chapter") return ChapterResult{3, std::string(512, 'a')};
  if (mode == "percent") return PercentResult{42};
  if (mode == "clip" || mode == "lookup" || mode == "bookmark")
    return ClippingResult{.action = mode == "clip" ? ClippingResult::Action::Clip :
                                    mode == "lookup" ? ClippingResult::Action::Lookup : ClippingResult::Action::Bookmark,
                          .text = std::string(4096, 'x'), .startPageOffset = 0, .endPageOffset = 1,
                          .startWordIndex = 2, .endWordIndex = 9, .wordCount = 8,
                          .startOffset = 123, .endOffset = 456};
  if (mode == "bookmarks" || mode == "clippings")
    return ProgressChangeResult{.spineIndex = 4, .page = 3, .totalPages = 10, .xpath = "target-xpath",
                                .percentage = .375f, .hasSavedProgress = true,
                                .hasVisibleTextOffset = true, .visibleTextOffset = 321};
  if (mode == "footnote" || mode == "footnote-page") return FootnoteResult{"target.xhtml#anchor"};
  if (mode == "wifi") return WifiResult{true, "ssid", "ip"};
  return ActivityResult{};
}
void launch(const std::string& mode) {
  assert(xp_app_test_current_epub() == results.owner && xp_app_test_parent_count() == 0);
  if (mode == "menu") xp_epub_test_menu_open(results.owner);
  else if (mode == "text-overlay") {
    results.toolbarMode = true; xp_epub_test_menu_open(results.owner);
    assert(results.toolbar && !xp_app_test_pending_kind());
    xp_epub_test_text_settings_open(results.owner, 1);
  }
  else if (mode == "footnote-page") xp_epub_test_child_footnotes(results.owner, 0);
  else if (mode == "wifi") {
    if (results.popupChoice) results.popupChoice(results.popupContext, 0);
    else xp_epub_test_recovery_begin(results.owner);
  }
  else {
    const std::map<std::string, int> actions{{"chapter", 0}, {"footnote", 1}, {"text", 2},
      {"percent", 6}, {"bookmarks", 9}, {"clip", 10}, {"lookup", 10}, {"bookmark", 10},
      {"clippings", 11}, {"qr", 14}, {"dictionary", 18}};
    xp_epub_test_menu_confirm(results.owner, actions.at(mode));
  }
  assert(xp_app_test_pending_kind() == 1);
  assert(xp_app_test_drain() == 1 && xp_app_test_parent_count() == 1);
  assert(xp_app_test_current_legacy() && !xp_app_test_current_epub());
}
void closeToSleep() {
  XpAppEvent render{}; render.kind = XP_APP_RENDER_REQUESTED; render.value.render_immediate = 1;
  assert(xp_app_event(&render) == XP_APP_ACCEPTED && results.notificationPending);
  // The already posted notification is intentionally serviced after real SleepRequested cleanup.
  XpAppEvent sleep{}; sleep.kind = XP_APP_SLEEP_REQUESTED;
  assert(xp_app_event(&sleep) == XP_APP_ACCEPTED);
  assert(!xp_app_test_current_epub() && xp_app_test_parent_count() == 0);
  assert(results.children.empty() && results.pages.empty() && !results.section && !results.toolbar);
  assertExited();
  void* lock = runtime.render_lock(); XpAppRenderToken token{lock};
  xp_app_render(&token); runtime.render_unlock(lock);
  assert(results.sleepPainted); results.notificationPending = false;
  popFinalSleep();
}
}  // namespace

int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string mode = argv[1]; const bool cancelled = std::strtoul(argv[2], nullptr, 10) != 0;
  Fixture state; fixture = &state;
  bind(); bindResultsLifecycle(); bindResultsPhysical(); initialize();
  state.startAttempt(); assert(install() == 1); assertEntered();
  results.owner = xp_app_test_current_epub(); paintOwner();
  if (mode == "stack-failure") {
    xp_epub_test_menu_open(results.owner);
    assert(xp_app_test_pending_kind() == 1 && xp_app_test_parent_count() == 0);
    results.allocatingStack = true; xp_test_fail_allocation_after(0);
    const auto drained = xp_app_test_drain(); const auto attempts = xp_test_allocation_attempts();
    xp_test_fail_allocation_after(SIZE_MAX); results.allocatingStack = false;
    assert(drained == 1 && attempts == 1 && results.born == 1 && results.dead == 1);
    assert(xp_app_test_current_epub() == results.owner && xp_app_test_parent_count() == 0);
    assert(xp_app_test_pending_kind() == 0 && results.returns == 0);
    requireResult(!xp_epub_test_has_child(results.owner), "APP_STACK_INTENT_RETAINED");
  } else if (mode == "pending-shutdown") {
    launch("text"); // A retained reader plus an active native child are both destroyed.
  } else {
    for (unsigned repeat = 0; repeat < (mode == "wifi" && cancelled ? 1u : 2u); ++repeat) {
      if (!results.section) paintOwner();
      launch(mode);
      if (mode == "dictionary") {
        queuePop(payload("lookup"), false); // Real Selection -> Dictionary push.
        assert(nativeActivity(xp_app_test_current_legacy())->kind == "dictionary");
      }
      results.deferParentPop = mode == "wifi" && cancelled;
      queuePop(payload(mode), cancelled);
      if (mode == "lookup" && !cancelled) {
        assert(nativeActivity(xp_app_test_current_legacy())->kind == "dictionary");
        queuePop(ActivityResult{}, true);
      }
      if (mode == "dictionary" && !cancelled) {
        assert(nativeActivity(xp_app_test_current_legacy())->kind == "selection");
        queuePop(ActivityResult{}, true); // Real recursive dictionary completion launches new selection.
      }
      if (xp_app_test_current_legacy()) menuDismiss();
      assert(xp_app_test_current_epub() == results.owner);
      XpEpubTestMenuObservation observation{};
      xp_epub_test_menu_observe(results.owner, &observation);
      if (mode == "menu") requireResult(!xp_epub_test_has_child(results.owner), "APP_RESULT_INTENT_RETAINED");
      if (!cancelled && mode == "menu") assert(observation.screenshot);
      if (!cancelled && mode == "chapter") assert(observation.spine == 3 && observation.next_page == 0);
      if (!cancelled && mode == "percent") assert(observation.spine == 2 && observation.next_page == 0);
      if (!cancelled && (mode == "bookmarks" || mode == "clippings")) assert(observation.spine == 4 && observation.next_page == 3);
      if (!cancelled && mode == "clip") assert(results.inserts == repeat + 1 && results.exports == repeat + 1);
      if (!cancelled && mode == "bookmark") assert(results.bookmarkWrites == repeat + 1);
      if (mode == "text-overlay") {
        assert(observation.overlay == 3 && observation.has_toolbar && !observation.has_section);
        assert(results.settingsSaved == repeat + 1 && results.toolbarBegins == (repeat + 1) * 2);
        xp_epub_test_overlay_close(results.owner);
      }
      if (mode == "chapter" && !cancelled) {
        xp_epub_test_menu_anchor(results.owner, [](void*, XpAppText value) -> uint8_t {
          assert(text(value) == std::string(512, 'a')); return 1;
        }, nullptr);
      }
    }
  }
  results.deferParentPop = false;
  closeToSleep();
  if ((mode == "footnote" || mode == "footnote-page") && !cancelled) {
    const std::vector<uint8_t> expected{2, 1, 0, 2, 0, 3, 0, 2, 0};
    assert(results.savedLinks == expected);
  } else assert(results.savedLinks.empty());
  assert(xp_test_live_allocations() == (mode == "stack-failure" ? 0u : 1u)); // App retains its empty parent Vec capacity.
  assert(results.born == results.dead && results.firstDeliveryLocks == results.returns);
  assert(results.borrowedChecks == 2 * results.returns);
  assert(results.sectionOpened == results.sectionClosed && state.handles.empty());
  assert(!state.renderDepth && !state.modelDepth);
  state.dump();
  std::printf("app-results mode=%s cancelled=%u returns=%u children=%u pages=0 sections=%u\n",
              mode.c_str(), cancelled, results.returns, results.born, results.sectionOpened);
}
