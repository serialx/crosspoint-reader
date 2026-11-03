// Appended to the actual native toolbar resource fixture by the draft driver.
#include "overlay_actions_session_fixture.h"
#include "overlay_view_session_fixture.h"

static unsigned ownerModelDepth, ownerResourceDepth, ownerSectionCloses, ownerLogs;
static unsigned ownerPaints, ownerRows, ownerTexts;
static int bookMarker, sectionMarker, rootMarker;
static ReaderToolbarUi* ownerToolbar;
static bool ownerRowFailure;
static int ownerInterleave;
static XpUiListState ownerInputNav;

static void ownerPhysical() {
  assert(ownerResourceDepth == 1 && ownerModelDepth == 0 && criticalDepth == 0);
}
static void ownerObserveText(const XpUiCommand& command, const char* value) {
  ownerPhysical();
  if (command.resource > 0 && command.resource < XP_UI_READER_HEADER) {
    const int index = (command.resource - 1) / 2;
    const bool label = (command.resource - 1) % 2 == 0;
    const size_t prefix = label ? 2 * (index % 5) : 0;
    assert(std::strlen(value) == (label ? 64 + prefix : 0));
    for (size_t i = 0; label && i < 64 + prefix; ++i)
      assert(value[i] == (i < prefix ? ' ' : static_cast<char>('A' + index % 26)));
    ++ownerTexts;
  }
}
static void ownerPaintInterleave() {
  ownerPhysical();
  ++ownerPaints;
  if (ownerInterleave == 1) resetToolbar(ownerToolbar, 3, 3);
  if (ownerInterleave == 2) assert(scrollToolbar(ownerToolbar, 7, 30));
  ownerInputNav = ownerToolbar->nav_;
}
static const XpAppRuntime ownerRuntime = [] {
  auto table = overlayUnexpectedRuntime();
  table.model_lock = [] { assert(ownerResourceDepth == 1 && ownerModelDepth == 0); ++ownerModelDepth; };
  table.model_unlock = [] { assert(ownerModelDepth == 1); --ownerModelDepth; };
  return table;
}();
static const XpAppResources ownerResources = [] {
  auto table = overlayUnexpectedResources();
  table.translate = [](uint16_t) -> XpAppText { ownerPhysical(); return text("Contents"); };
  return table;
}();
static const LegacyEpubLeaves ownerLeaves = [] {
  auto table = overlayLegacyLeaves();
  table.physical.section_page_count = [](void* p) -> uint16_t { ownerPhysical(); assert(p == &sectionMarker); return 10; };
  table.physical.section_estimated_pages = [](void* p) -> int32_t { ownerPhysical(); assert(p == &sectionMarker); return 10; };
  table.physical.section_building = [](void* p) -> uint8_t { ownerPhysical(); assert(p == &sectionMarker); return 0; };
  table.physical.section_complete = [](void* p) -> uint8_t { ownerPhysical(); assert(p == &sectionMarker); return 1; };
  table.physical.section_partial = [](void* p) -> uint8_t { ownerPhysical(); assert(p == &sectionMarker); return 0; };
  table.physical.section_close = [](void* p) { ownerPhysical(); assert(p == &sectionMarker); ++ownerSectionCloses; };
  table.physical.input_facts = [](XpEpubInputFacts* out) { ownerPhysical(); *out = {}; out->touch = 1; };
  table.book.book_toc_count = [](void* p) -> int32_t { ownerPhysical(); assert(p == &bookMarker); return 30; };
  table.book.book_toc = [](void* p, int32_t index, XpEpubTocSink sink, void* context) -> uint8_t {
    ownerPhysical(); assert(p == &bookMarker && index >= 0 && index < 30);
    ++ownerRows;
    if (ownerRowFailure) return 0;
    char temporary[64]; std::memset(temporary, 'A' + index % 26, sizeof(temporary));
    XpEpubToc row{};
    row.title = {reinterpret_cast<const uint8_t*>(temporary), sizeof(temporary)};
    row.level = index % 5 + 1;
    const uint8_t result = sink(context, &row);
    assert(result == 1);
    std::memset(temporary, '!', sizeof(temporary));
    return result;
  };
  table.physical.toolbar_paint = [](void* raw, const XpEpubToolbarModel* model, XpEpubRowSource rows,
                            void* context) -> uint8_t {
    ownerPhysical(); assert(raw == ownerToolbar && model && rows && context);
    assert(model->panel == 1 && model->dense == 0 && model->item_count == 30 && model->selected == 5);
    return paintToolbar(raw, model, rows, context);
  };
  table.physical.log = [](uint8_t severity, XpAppText tag, XpAppText message) {
    ownerPhysical(); assert(severity == 1);
    assert(tag.len == 3 && std::memcmp(tag.data, "RUI", 3) == 0);
    assert(message.len == 27 && std::memcmp(message.data, "Cannot build reader toolbar", 27) == 0);
    ++ownerLogs;
  };
  table.physical.book_set_extractor = [](void* handle) {
    if (handle) ownerLeaves.book.book_install_extractor(handle);
    else ownerLeaves.book.book_clear_extractor();
  };
  return table;
}();
static XpEpubTestOverlayViewSeed ownerSeed(int missing) {
  XpEpubTestOverlayViewSeed seed{};
  seed.native = &rootMarker;
  seed.book = missing == 1 ? nullptr : &bookMarker;
  seed.section = missing == 2 ? nullptr : &sectionMarker;
  seed.toolbar = missing == 3 ? nullptr : ownerToolbar;
  seed.spine = 2; seed.page = 1; seed.focus = 0; seed.index = 5;
  seed.overlay = 2; seed.cursor = 1; seed.real_values = 1;
  // Complete typed defaults; real_values=1 installs no OverlayViewHooks.
  seed.chapter = &OverlayUnexpected<decltype(seed.chapter)>::call;
  seed.text_name = &OverlayUnexpected<decltype(seed.text_name)>::call;
  seed.text_value = &OverlayUnexpected<decltype(seed.text_value)>::call;
  seed.more_name = &OverlayUnexpected<decltype(seed.more_name)>::call;
  seed.more_value = &OverlayUnexpected<decltype(seed.more_value)>::call;
  return seed;
}
static bool ownerSceneFailed(void* owner) {
  XpEpubTestOverlayObservation result{};
  xp_epub_test_overlay_observe(owner, &result);
  return result.failed != 0;
}
[[noreturn]] static void publicationFailure(const char* marker) {
  std::fprintf(stderr, "%s\n", marker);
  std::fflush(stderr);
  std::_Exit(42);  // Controlled mutant verdict; do not run leak teardown on this deliberately stopped case.
}
static void ownerCase(int interleave, bool paintFailure, bool rowFailure, int missing = 0) {
  assert(!ownerResourceDepth && !ownerModelDepth && !criticalDepth);
  ++ownerResourceDepth;  // Caller owns the real render scope; no nested acquisition in render_overlay.
  ownerToolbar = static_cast<ReaderToolbarUi*>(openToolbar());
  assert(ownerToolbar);
  beginToolbar(ownerToolbar);
  assert(ownerToolbar->commands.reserve(128));
  resetToolbar(ownerToolbar, 0, 0);
  auto seed = ownerSeed(missing);
  const unsigned closed = ownerSectionCloses;
  void* owner = xp_epub_test_overlay_view_create_legacy_book(&seed,&ownerLeaves.physical, &ownerRuntime, &ownerResources, &ownerLeaves.book);
  assert(owner);
  ownerInterleave = interleave; ownerRowFailure = rowFailure;
  ownerPaints = ownerRows = ownerTexts = ownerLogs = 0;
  renderer.paintSucceeds = !paintFailure;
  renderer.longRows = false;
  const auto beforeNav = ownerToolbar->nav_;
  const auto beforeHits = ownerToolbar->published;
  observeOwnerText = ownerObserveText;
  beforeOwnerPaintReturn = ownerPaintInterleave;
  xp_epub_test_overlay_view_render(owner);
  cleared(*ownerToolbar);
  if (missing) {
    assert(ownerPaints == 0 && ownerRows == 0 && ownerTexts == 0 && ownerLogs == 0);
    assert(!ownerSceneFailed(owner));
    sameNav(beforeNav, ownerToolbar->nav_);
  } else if (paintFailure || rowFailure) {
    if (paintFailure && ownerToolbar->ready)
      publicationFailure("PUBLICATION_FAILURE: failed-paint-marked-ready");
    assert(!ownerToolbar->ready && ownerSceneFailed(owner) && ownerLogs == 1);
    assert(ownerToolbar->contact.contact_held == 0);
    sameNav(beforeNav, ownerToolbar->nav_);
    assert(std::memcmp(&beforeHits, &ownerToolbar->published, sizeof(beforeHits)) == 0);
    assert(ownerPaints == (rowFailure ? 0u : 1u));
    // Retry traverses the same ordinary owner -> native -> widget path.
    ownerRowFailure = false; renderer.paintSucceeds = true;
    xp_epub_test_overlay_view_render(owner);
    assert(ownerToolbar->ready && !ownerSceneFailed(owner) && ownerLogs == 1);
    cleared(*ownerToolbar);
  } else {
    assert(ownerToolbar->ready && !ownerSceneFailed(owner) && ownerLogs == 0);
    assert(ownerPaints == 1 && ownerRows > 0 && ownerTexts > 0);
    if (interleave) {
      if (ownerToolbar->nav_.navigation_revision != ownerInputNav.navigation_revision)
        publicationFailure("PUBLICATION_FAILURE: stale-navigation-publication");
      sameNav(ownerInputNav, ownerToolbar->nav_);
    }
  }
  observeOwnerText = nullptr; beforeOwnerPaintReturn = nullptr;
  xp_epub_test_overlay_view_destroy(owner);
  assert(ownerSectionCloses == closed + (missing == 2 ? 0u : 1u));
  closeToolbar(ownerToolbar); ownerToolbar = nullptr;
  --ownerResourceDepth;
  assert(!ownerModelDepth && !criticalDepth);
}
int main(int argc, char** argv) {
  if (argc == 1) {
    resource_contract_main();  // All original resource assertions run unchanged in the baseline.
  } else {
    assert(argc == 2 && std::strcmp(argv[1], "--owner-only") == 0);
  }
  ownerCase(0, false, false);
  ownerCase(1, false, false);
  ownerCase(2, false, false);
  ownerCase(0, true, false);
  ownerCase(0, false, true);
  for (int missing : {1, 2, 3}) ownerCase(0, false, false, missing);
  std::puts("draft owner toolbar composition: real Contents rows, native publication, failure/retry and missing resources");
}
