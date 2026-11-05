// Two translation units keep the original widget fakes separate from App physical fakes.
#ifdef XP_RECURSIVE_DICTIONARY_OWNER
namespace {
Fixture recursiveState;
bool recursiveInitialized = false;
unsigned recursiveCases = 0;
}

extern "C" uint8_t xp_recursive_dictionary_result(const ActivityResult* original) {
  assert(original && original->data.index() == 0);
  if (!recursiveInitialized) {
    fixture = &recursiveState;
    bind(); bindResultsLifecycle(); bindResultsPhysical();
    initialize();
    recursiveInitialized = true;
  } else {
    assert(!xp_app_test_current_epub() && !xp_app_test_current_legacy());
    assert(!xp_app_test_is_home() && !xp_app_test_parent_count() && !xp_app_test_pending_kind());
    assert(recursiveState.handles.empty() && results.children.empty() && results.pages.empty());
    assert(!recursiveState.renderDepth && !recursiveState.modelDepth);
    recursiveState = Fixture{};
    results = Results{};
  }
  const size_t before = xp_test_live_allocations();
  recursiveState.startAttempt();
  assert(install() == 1);
  assertEntered();
  results.owner = xp_app_test_current_epub();
  paintOwner();
  assert(results.section && results.rendered > 0 && results.pages.empty());

  // Real menu action -> Selection; successful Lookup result -> Dictionary.
  // The original widget supplied only the final viewer result, not these admissions.
  launch("dictionary");
  assert(nativeActivity(xp_app_test_current_legacy())->kind == "selection");
  assert(results.born == 1 && !results.dead && !results.lookups && xp_epub_test_has_child(results.owner));
  queuePop(payload("lookup"), false);
  assert(nativeActivity(xp_app_test_current_legacy())->kind == "dictionary");
  assert(results.lookups == 1 && results.born == 2 && results.dead == 1);
  assert(xp_app_test_parent_count() == 1 && xp_epub_test_has_child(results.owner));

  // Preserve the complete original ActivityResult. The actual native producer,
  // App destruction/publication/unlock, and child_result consume it synchronously.
  const auto returnsBefore = results.returns;
  queuePop(*original, original->isCancelled);
  assert(results.returns == returnsBefore + 1 && results.dead == 2);
  void* const returnedChild = xp_app_test_current_legacy();
  const bool reopened = returnedChild != nullptr;
  if (returnedChild) {
    assert(nativeActivity(returnedChild)->kind == "selection");
    assert(results.born == 3 && xp_app_test_parent_count() == 1);
    assert(xp_epub_test_has_child(results.owner));
    // Cleanup is a further real cancelled Selection result, never an intent reset.
    queuePop(ActivityResult{}, true);
    if (xp_app_test_current_legacy()) menuDismiss();
  } else {
    assert(results.born == 2 && xp_app_test_parent_count() == 0);
  }
  assert(xp_app_test_current_epub() == results.owner && !xp_app_test_current_legacy());
  assert(!xp_epub_test_has_child(results.owner) && !xp_app_test_pending_kind());
  assert(results.savedLinks.empty() && !results.inserts && !results.exports && !results.bookmarkWrites);
  closeToSleep();
  assert(results.children.empty() && results.pages.empty() && !results.section && !results.bound);
  assert(results.born == results.dead && results.sectionOpened == results.sectionClosed);
  assert(results.firstDeliveryLocks == results.returns && results.borrowedChecks == 2 * results.returns);
  assert(recursiveState.handles.empty() && !recursiveState.renderDepth && !recursiveState.modelDepth);
  // Only the App's reusable parent Vec capacity survives its first completed flow.
  assert(xp_test_live_allocations() == before + (recursiveCases == 0 ? 1u : 0u));
  ++recursiveCases;
  std::printf("recursive-owner case=%u returned-selection=%u returns=%u children=%u\n",
              recursiveCases, unsigned(reopened), results.returns, results.born);
  return uint8_t(reopened);
}
extern "C" unsigned xp_recursive_dictionary_cases() { return recursiveCases; }
#else
extern "C" uint8_t xp_recursive_dictionary_result(const ActivityResult*);
extern "C" unsigned xp_recursive_dictionary_cases();

void DictionaryDefinitionActivity::onLookupResult(const ActivityResult& result) {
  // Observation of a real physical selection child, not a cancellation predicate.
  selectionOpened = xp_recursive_dictionary_result(&result) != 0;
}
#endif
