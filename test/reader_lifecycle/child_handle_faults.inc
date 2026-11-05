extern "C" void xp_epub_test_child_sync(void*);
static uint16_t compositionPages = 10;

void enterCompositionReader() {
  f().startAttempt();
  XpAppEvent event{}; event.kind = XP_APP_NAVIGATE; event.value.navigate.kind = XP_APP_READER;
  event.value.navigate.value.reader = {wire(f().path), 0};
  assert(xp_app_event(&event) == XP_APP_ACCEPTED); assertEntered();
  results.owner = xp_app_test_current_epub(); paintOwner();
}
void exerciseNativeChild(void* raw) {
  assert(raw && results.children.count(raw));
  const auto facts = legacy.facts(raw, 1, 1);
  assert(facts.reader == (nativeActivity(raw)->kind == "selection"));
  legacy.loop(raw);
  void* held = runtime.render_lock(); RenderLock lock; XpAppRenderToken token{&lock};
  legacy.render(raw, &token); runtime.render_unlock(held);
  assert(!legacy.reader_turn(raw, 1, 0));
  legacy.screenshot_info(raw, [](void*, const XpAppScreenshot* info) { assert(info); }, nullptr);
}
void openFactory(const std::string& leaf) {
  if (leaf == "menu") xp_epub_test_menu_open(results.owner);
  else if (leaf == "selection") xp_epub_test_child_selection(results.owner, -1, -1, 0);
  else if (leaf == "wifi") xp_epub_test_recovery_begin(results.owner);
  else if (leaf == "sync") xp_epub_test_child_sync(results.owner);
  else {
    const std::map<std::string, int> actions{{"chapter",0},{"footnote",1},{"text_settings",2},
      {"percent",6},{"bookmarks",9},{"clippings",11},{"qr",14}};
    xp_epub_test_menu_confirm(results.owner, actions.at(leaf));
  }
}
void finishComposition() {
  closeToSleep();
  assert(results.children.empty() && results.pages.empty() && results.born == results.dead);
  assert(!results.section && !results.toolbar && f().handles.empty());
  assert(!f().renderDepth && !f().modelDepth);
  assert(xp_test_live_allocations() <= 1); // Only the App parent-vector capacity can remain.
}
void nativeFactoryFailure(const std::string& leaf, unsigned stage, unsigned pageCount) {
  const auto begin = results.born;
  if (leaf == "dictionary") launch("clip");
  const auto firstEvent = f().events.size();
  outerAttempt = 0; failOuterAfter = stage;
  if (leaf == "dictionary") queuePop(payload("lookup"), false);
  else openFactory(leaf);
  failOuterAfter = SIZE_MAX;
  assert(outerAttempt == stage + 1);
  // Wi-Fi OOM deliberately finishes the reader; the pending Pop is superseded by Sleep below.
  assert(xp_app_test_pending_kind() == (leaf == "wifi" ? 2 : 0));
  assert(!xp_app_test_current_legacy());
  assert(xp_app_test_current_epub() == results.owner && !xp_app_test_parent_count());
  assert(!xp_epub_test_has_child(results.owner) && results.pages.empty());
  assert(results.born == begin + unsigned(leaf == "dictionary"));
  if (leaf == "selection") {
    unsigned opened = 0, closed = 0;
    for (size_t i = firstEvent; i < f().events.size(); ++i) {
      opened += f().events[i].name == "page-open";
      closed += f().events[i].name == "page-close";
    }
    assert(opened == pageCount && closed == pageCount);
  }
  finishComposition();
}
void syncReplacement() {
  openFactory("sync");
  assert(xp_app_test_pending_kind() == 3 && xp_app_test_parent_count() == 0);
  assert(xp_app_test_drain());
  auto* raw = xp_app_test_current_legacy();
  assert(raw && nativeActivity(raw)->kind == "sync" && !xp_app_test_current_epub());
  exerciseNativeChild(raw);
  // Native sync completes by opening the reader, rather than returning a child result.
  f().startAttempt();
  XpAppEvent event{}; event.kind = XP_APP_NAVIGATE; event.value.navigate.kind = XP_APP_READER;
  event.value.navigate.value.reader = {wire(f().path), 0};
  assert(xp_app_event(&event) == XP_APP_ACCEPTED && xp_app_test_drain());
  assert(results.born == results.dead && results.children.empty());
  results.owner = xp_app_test_current_epub();
  results.wifiMatrix = results.syncSucceeded = true; // Existing two-generation teardown assertions.
  paintOwner(); finishComposition();
}
void capabilityCase(const std::string& kind) {
  std::unique_ptr<OwnedLegacyActivity> owner;
  if (kind == "interval") owner = makeLegacyActivity<IntervalSelectionActivity>("interval");
  else if (kind == "reader") owner = makeLegacyActivity<ReaderActivity>("reader");
  else if (kind == "xtc") owner = makeLegacyActivity<XtcReaderActivity>("xtc");
  else owner = makeLegacyActivity<ClipSelectionActivity>(renderer, input,
      std::vector<std::unique_ptr<Page>>{}, 0, 0, 0, 0, ClipSelectionActivity::Mode::Clip);
  assert(owner);
  const auto facts = production_facts(owner.get(), 0, 1);
  assert(facts.reader && !facts.input_over_page);
  const bool realReader = kind == "reader" || kind == "xtc";
  assert(production_reader_turn(owner.get(), 1, 0) == realReader);
  assert(production_reader_turn(owner.get(), 0, 1) == realReader);
  if (realReader) {
    const auto* reader = owner->activity->readerControls();
    assert(reader && reader->turns == 1 && reader->skips == 1);
  }
  owner.reset();
}
void nestedResult(bool failWrapper) {
  launch("text");
  auto* parent = nativeActivity(xp_app_test_current_legacy());
  bool delivered = false;
  auto callback = [&](const ActivityResult& result) {
    assert(!f().renderDepth && result.data.index() == 2);
    assert(std::get<KeyboardResult>(result.data).text == "nested result"); delivered = true;
  };
  auto child = std::make_unique<Activity>("nested");
  failOuterAfter = failWrapper ? 0 : SIZE_MAX; outerAttempt = 0;
  parent->startActivityForResult(std::move(child), std::move(callback));
  failOuterAfter = SIZE_MAX;
  if (failWrapper) { assert(!xp_app_test_pending_kind() && !delivered && parent->resultHandler); parent->resultHandler = nullptr; }
  else {
    assert(xp_app_test_pending_kind() == 1 && xp_app_test_drain());
    auto* raw = xp_app_test_current_legacy();
    nativeActivity(raw)->result = KeyboardResult{"nested result"};
    // Observe the same production with_result without EPUB-only continuation assertions.
    const auto withResult = legacy.with_result;
    legacy.with_result = production_with_result;
    XpAppEvent event{}; event.kind = XP_APP_POP; assert(xp_app_event(&event) == XP_APP_ACCEPTED && xp_app_test_drain());
    legacy.with_result = withResult;
    assert(delivered && !parent->resultHandler && nativeActivity(xp_app_test_current_legacy()) == parent);
  }
  finishComposition();
}
int specialComposition(const std::string& mode) {
  Fixture state; fixture = &state;
  bind(); bindResultsLifecycle(); bindResultsPhysical(); bindProductionChildren(); initialize();
  if (mode.starts_with("capability-")) {
    capabilityCase(mode.substr(11));
    assert(results.born == results.dead); return 0;
  }
  if (mode.starts_with("page-fail-")) {
    compositionPages = uint16_t(2 + unsigned(mode[10]-'0'));
    legacyBook.section.section_page_count = [](void*) -> uint16_t { return compositionPages; };
    legacyBook.section.section_estimated_pages = [](void*) -> int32_t { return compositionPages; };
  }
  enterCompositionReader();
  if (mode.starts_with("factory-fail-")) {
    const auto stage = mode.rfind('-'); const auto leaf = mode.substr(13, stage-13);
    nativeFactoryFailure(leaf, unsigned(mode.back()-'0'), 3);
  } else if (mode.starts_with("page-fail-")) {
    nativeFactoryFailure("selection", unsigned(mode.back()-'0'), unsigned(mode[10]-'0'));
  } else if (mode == "sync-replace") syncReplacement();
  else if (mode == "nested-result" || mode == "nested-failure") nestedResult(mode == "nested-failure");
  else if (mode == "selection-metadata-failure") {
    const auto before = results.born;
    xp_test_fail_allocation_after(0); openFactory("selection");
    assert(xp_test_allocation_attempts() == 1); xp_test_fail_allocation_after(SIZE_MAX);
    assert(results.born == before && results.pages.empty() && !xp_epub_test_has_child(results.owner));
    finishComposition();
  } else {
    openFactory("selection");
    assert(xp_app_test_pending_kind() == 1 && results.pages.size() == 3);
    if (mode == "selection-stack-failure") {
      results.allocatingStack = true; xp_test_fail_allocation_after(0);
      assert(xp_app_test_drain()); xp_test_fail_allocation_after(SIZE_MAX); results.allocatingStack = false;
      requireResult(!xp_epub_test_has_child(results.owner), "APP_STACK_INTENT_RETAINED");
      assert(results.pages.empty() && results.children.empty());
    } else if (mode == "pending-supersede") {
      openFactory("menu"); assert(results.pages.empty());
    } else assert(mode == "selection-pending-shutdown");
    finishComposition();
  }
  std::printf("production composition %s: children=%u/%u pages=0\n", mode.c_str(), results.born, results.dead);
  return 0;
}
