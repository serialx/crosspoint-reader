static std::string menuSnapshot(MenuSessionBridge& session) {
  XpEpubTestMenuObservation out{};
  xp_epub_test_menu_observe(session.rust, &out);
  std::ostringstream text;
  text << out.cached_spine << ',' << out.cached_count << ',' << out.next_page << ',' << out.spine << ','
       << out.focus << ',' << out.bookmark_time << ',' << int(out.manual_turn) << ',' << int(out.automatic) << ','
       << int(out.overlay) << ',' << int(out.stored) << ',' << int(out.hold) << ',' << int(out.cursor) << ','
       << int(out.bookmarked) << ',' << int(out.screenshot) << ',' << int(out.has_section) << ','
       << int(out.has_toolbar) << ',' << int(out.more_count);
  for (unsigned i = 0; i < 19; ++i) text << ',' << int(out.more_actions[i]);
  session.observe();
  text << ',' << session.owner.pendingAnchor;
  return text.str();
}
static void moreCase(bool percent, unsigned rejectAt) {
  admissionRejectAt = rejectAt;
  admissionAcquisitions = 0;
  admissionEvents.clear();
  EpubReaderActivity owner;
  owner.overlay = EpubReaderActivity::Overlay::More;
  owner.overlayPageStored = true;
  owner.panelHoldJumped = false;
  owner.section = std::make_unique<Section>();
  using Action = EpubReaderMenuActivity::MenuAction;
  owner.moreItems.push_back({percent ? Action::GO_TO_PERCENT : Action::BOOKMARKS, STR_PORTRAIT});
  MenuSessionBridge session(owner);
  const auto before = menuSnapshot(session);
  owner.activateMoreRow(0);
  XpEpubTestOverlayObservation gate{};
  xp_epub_test_overlay_observe(session.rust, &gate);
  const auto after = menuSnapshot(session);
  const auto creates = countEvent("child-new"), starts = countEvent("child-start");
  const auto discards = countEvent("discard"), restores = countEvent("restore"), settles = countEvent("settle");
  if (rejectAt) {
    requireAdmission(admissionAcquisitions == rejectAt && settles == rejectAt - 1 &&
                     discards == 0 && restores == 0 && creates == 0 && starts == 0 &&
                     !owner.child && !owner.callback && countEvent("update") == 1 &&
                     gate.failed && after == before && owner.overlay == EpubReaderActivity::Overlay::More,
                     rejectAt == 1 ? "MORE_FIRST_ADMISSION" : "MORE_SECOND_ADMISSION");
    requireAdmission(countEvent("position") == 0 && countEvent("percent") == 0 &&
                     countEvent("path") == 0 && countEvent("load-bookmarks") == 0 &&
                     countEvent("release-strips") == 0,
                     "rejected More performed action effects");
    const auto events = admissionEvents.size();
    xp_epub_test_menu_result(session.rust, nullptr);
    requireAdmission(menuSnapshot(session) == after && admissionEvents.size() == events,
                     "rejected More retained child intent");
    XpEpubTestOverlayObservation resultGate{};
    xp_epub_test_overlay_observe(session.rust, &resultGate);
    requireAdmission(sameOverlay(gate, resultGate), "null result changed rejection state");
  } else {
    requireAdmission(admissionAcquisitions == 2 && settles == (percent ? 2u : 1u) &&
                     discards == (percent ? 0u : 1u) && restores == unsigned(percent) &&
                     creates == 1 && starts == 1 && countEvent("child-drop") == 0 && owner.child &&
                     owner.callback && countEvent("update") == 1 && !gate.failed &&
                     owner.overlay == EpubReaderActivity::Overlay::None && !owner.overlayPageStored,
                     "MORE_SUCCESS");
    requireAdmission(std::string(owner.child->kind) == (percent ? "percent" : "bookmarks"),
                     "More chose wrong physical child");
    requireAdmission(!percent || hasEventValue("arg", 42), "Percent child lost captured position");
    std::vector<std::string> selected;
    for (const auto& e : admissionEvents)
      if (e.name == "settle" || e.name == "discard" || e.name == "restore" ||
          e.name == "child-new" || e.name == "child-start" || e.name == "update") selected.push_back(e.name);
    const auto expected = percent
        ? std::vector<std::string>{"settle", "settle", "restore", "child-new", "child-start", "update"}
        : std::vector<std::string>{"settle", "discard", "child-new", "child-start", "update"};
    requireAdmission(selected == expected, "More transition order");
    ActivityResult result;
    result.isCancelled = false;
    if (percent) result.data = PercentResult{37};
    else result.data = ProgressChangeResult{91};
    auto callback = std::move(owner.callback);
    owner.child.reset();
    event("delivered-result");
    callback(result);
    requireAdmission(countEvent("child-drop") == 1 &&
                     countEvent("jump") == unsigned(percent) &&
                     countEvent("progress") == unsigned(!percent) &&
                     countEvent("load-bookmarks") == unsigned(!percent) &&
                     (percent ? hasEventValue("jump", 37) : hasEventValue("progress", 91)),
                     "More retained result did not reach actual child intent");
    const auto events = admissionEvents.size();
    const auto settled = menuSnapshot(session);
    xp_epub_test_menu_result(session.rust, nullptr);
    requireAdmission(menuSnapshot(session) == settled && admissionEvents.size() == events,
                     "More result intent was not consumed");
  }
  for (const auto& e : admissionEvents) {
    if (e.name == "settle" || e.name == "discard" || e.name == "restore")
      requireAdmission(e.depth == 1, "More resource effect outside admitted scope");
    if (e.name == "child-new" || e.name == "child-start" || e.name == "child-drop")
      requireAdmission(e.depth == 0, "More child lifetime inside ResourceAccess");
  }
  requireAdmission(!lockDepth && !menuModelDepth, "More lock imbalance");
  std::printf("PASS more percent=%u reject=%u\n", unsigned(percent), rejectAt);
}
int main(int argc, char** argv) {
  if (argc != 3 || std::strlen(argv[1]) != 1 || std::strlen(argv[2]) != 1 ||
      argv[1][0] < '0' || argv[1][0] > '1' || argv[2][0] < '0' || argv[2][0] > '2') return 64;
  try { moreCase(argv[1][0] == '1', unsigned(argv[2][0] - '0')); }
  catch (const std::exception& error) {
    std::fprintf(stderr, "EXPECTED_ASSERTION: %s\n", error.what());
    return 42;
  }
  return 0;
}
