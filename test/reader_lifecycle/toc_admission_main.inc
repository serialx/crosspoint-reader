static void tocCase(int valid, unsigned rejectAt) {
  admissionRejectAt = rejectAt;
  admissionAcquisitions = 0;
  admissionEvents.clear();
  eventIndex = 0;
  EpubReaderActivity owner;
  owner.overlay = EpubReaderActivity::Overlay::Contents;
  owner.panelIndex = 0;
  owner.panelCursorShown = true;
  owner.overlayPageStored = true;
  owner.epub->tocSpine = valid ? 5 : -1;
  owner.currentSpineIndex = 3;
  owner.nextPageNumber = 8;
  owner.mappedInput.released = 1u << int(MappedInputManager::Button::Confirm);
  OverlayInputSessionBridge session(owner);
  owner.handleOverlayInput();
  XpEpubTestOverlayObservation after{};
  xp_epub_test_overlay_observe(session.rust, &after);
  requireAdmission(admissionAcquisitions == 1 && countEvent("update") == 1 &&
                   (rejectAt ? after.overlay == 2 && after.stored && after.failed &&
                               owner.currentSpineIndex == 3 && owner.nextPageNumber == 8 && owner.section &&
                               owner.pendingAnchor.empty() && countEvent("clear-position") == 0 &&
                               countEvent("section-drop") == 0 && countEvent("discard") == 0
                             : after.overlay == 0 && !after.stored && !after.failed &&
                               countEvent("discard") == 1 &&
                               owner.currentSpineIndex == (valid ? 5 : 3) &&
                               owner.nextPageNumber == (valid ? 0 : 8) && bool(owner.section) == !bool(valid) &&
                               owner.pendingAnchor == (valid ? "anchor" : "") &&
                               countEvent("clear-position") == unsigned(valid) &&
                               countEvent("section-drop") == unsigned(valid)),
                   rejectAt ? "TOC_ADMISSION" : "TOC_SUCCESS");
  requireAdmission(countEvent("toc-create") == 1 && countEvent("toc-drop") == 1 &&
                   !owner.result && countEvent("start-child") == 0 && !lockDepth && !overlayInputModelDepth,
                   "TOC ownership or lock imbalance");
  for (const auto& e : admissionEvents) {
    if (e.name == "clear-position" || e.name == "section-drop" || e.name == "discard")
      requireAdmission(e.depth == 1, "TOC transition outside admitted scope");
    if (e.name == "toc-create" || e.name == "toc-drop")
      requireAdmission(e.depth == 0, "TOC borrowed row outlived synchronous leaf");
  }
  if (!rejectAt) {
    std::vector<std::string> selected;
    for (const auto& e : admissionEvents)
      if (e.name == "toc-create" || e.name == "lock" || e.name == "clear-position" ||
          e.name == "section-drop" || e.name == "discard" || e.name == "unlock" ||
          e.name == "update" || e.name == "toc-drop") selected.push_back(e.name);
    auto expected = valid
        ? std::vector<std::string>{"toc-create", "lock", "clear-position", "section-drop", "discard",
                                   "unlock", "update", "toc-drop"}
        : std::vector<std::string>{"toc-create", "lock", "discard", "unlock", "update", "toc-drop"};
    requireAdmission(selected == expected, "TOC scope and borrowed row order");
  }
  const auto events = admissionEvents.size();
  const auto spine = owner.currentSpineIndex, next = owner.nextPageNumber;
  const auto anchor = owner.pendingAnchor;
  xp_epub_test_overlay_input_result(session.rust, nullptr);
  session.observe();
  XpEpubTestOverlayObservation result{};
  xp_epub_test_overlay_observe(session.rust, &result);
  requireAdmission(admissionEvents.size() == events && sameOverlay(after, result) &&
                   owner.currentSpineIndex == spine && owner.nextPageNumber == next && owner.pendingAnchor == anchor,
                   "TOC introduced a child intent");
  std::printf("PASS toc valid=%d reject=%u\n", valid, rejectAt);
}
int main(int argc, char** argv) {
  if (argc != 3 || std::strlen(argv[1]) != 1 || std::strlen(argv[2]) != 1 ||
      argv[1][0] < '0' || argv[1][0] > '1' || argv[2][0] < '0' || argv[2][0] > '1') return 64;
  try { tocCase(argv[1][0] - '0', unsigned(argv[2][0] - '0')); }
  catch (const std::exception& error) {
    std::fprintf(stderr, "EXPECTED_ASSERTION: %s\n", error.what());
    return 42;
  }
  return 0;
}
