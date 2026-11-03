// ACTUAL_CHILD_OBSERVATION
extern "C" void xp_epub_test_child_observe(void*, XpEpubTestChildObservation*);

void recordSelectionOutcome() {
  XpEpubTestChildObservation value{};
  xp_epub_test_child_observe(results.owner, &value);
  std::printf("SELECTION_OUTCOME %d %d %d %u %u %u %u %u %u %u %u %u %u %zu\n",
              value.page, value.next_page, value.spine, value.has_offset, value.visible_offset,
              value.dictionary, value.bookmark, value.clipping, value.sync_error,
              value.has_book, value.has_section, xp_app_test_pending_kind(),
              xp_epub_test_has_child(results.owner), results.pages.size());
}

void realSelectionAllocation(const std::string& mode) {
  const bool probe = mode == "selection-allocation-count";
  const size_t failAfter = probe ? SIZE_MAX : std::stoull(mode.substr(std::strlen("selection-allocation-")));
  const auto live = xp_test_live_allocations();
  const auto born = results.born;
  const auto pending = xp_app_test_pending_kind();
  auto* section = results.section;
  auto* toolbar = results.toolbar;
  XpEpubTestChildObservation before{}, after{};
  xp_epub_test_child_observe(results.owner, &before);
  assert(results.pages.empty() && !xp_epub_test_has_child(results.owner) && !pending);
  xp_test_fail_allocation_after(failAfter);
  openFactory("selection");
  const auto attempts = xp_test_allocation_attempts();
  xp_test_fail_allocation_after(SIZE_MAX);
  if (probe) {
    assert(attempts > 0 && xp_app_test_pending_kind() == 1 && results.pages.size() == 3);
    std::printf("REAL_SELECTION_ALLOCATION_COUNT %zu\n", attempts);
  } else {
    xp_epub_test_child_observe(results.owner, &after);
    assert(attempts > failAfter);
    assert(results.born == born && results.pages.empty() && !xp_epub_test_has_child(results.owner));
    assert(xp_app_test_pending_kind() == pending && xp_app_test_current_epub() == results.owner);
    assert(results.section == section && results.toolbar == toolbar);
    assert(before.page == after.page && before.next_page == after.next_page && before.spine == after.spine);
    assert(before.has_offset == after.has_offset && before.visible_offset == after.visible_offset);
    assert(before.dictionary == after.dictionary && before.bookmark == after.bookmark && before.clipping == after.clipping);
    assert(before.sync_error == after.sync_error && before.has_book == after.has_book && before.has_section == after.has_section);
    assert(xp_test_live_allocations() == live && readerBuffers.empty() && !readerCacheLocked);
    recordSelectionOutcome();
    std::printf("REAL_SELECTION_ALLOCATION_FAILURE %zu %zu\n", failAfter, attempts);
  }
  finishComposition();
}
