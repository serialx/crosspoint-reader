extern "C" void xp_epub_test_menu_confirm_direct(void*, int32_t);
static void confirmCase(bool committed, unsigned rejectAt) {
  EpubReaderActivity owner;
  owner.toolbar = false;
  owner.panelHoldJumped = false;
  MenuSessionBridge session(owner);
  if (committed) {
    owner.openReaderMenu();
    requireAdmission(owner.child && owner.callback && std::string(owner.child->kind) == "menu", "MENU_CHILD_SETUP");
  }
  const auto before = menuSnapshot(session);
  admissionEvents.clear();
  admissionAcquisitions = 0;
  admissionRejectAt = rejectAt;
  if (committed && rejectAt == 0) xp_epub_test_overlay_reject(session.rust);
  if (committed) {
    ActivityResult result;
    MenuResult menu;
    menu.action = int(EpubReaderMenuActivity::MenuAction::SCREENSHOT);
    menu.orientation = SETTINGS.orientation;
    menu.pageTurnOption = 0;
    result.data = menu;
    auto callback = std::move(owner.callback);
    owner.child.reset();
    event("delivered-result");
    callback(result);
  } else {
    xp_epub_test_menu_confirm_direct(session.rust, int(EpubReaderMenuActivity::MenuAction::SCREENSHOT));
  }
  XpEpubTestMenuObservation after{};
  xp_epub_test_menu_observe(session.rust, &after);
  constexpr unsigned acquisitions = XP_STRIP_RENDER ? 2 : 1;
  const bool accepted = committed || rejectAt == 0;
  const unsigned expectedLocks = accepted ? acquisitions : rejectAt;
  const unsigned expectedReleases = XP_STRIP_RENDER && (accepted || rejectAt > 1);
  const char* marker = rejectAt == 1 ? "MENU_CONFIRM_FIRST_ADMISSION" : "MENU_CONFIRM_SECOND_ADMISSION";
  requireAdmission(admissionAcquisitions == expectedLocks && countEvent("release-strips") == expectedReleases &&
                   bool(after.screenshot) == accepted && countEvent("update") == 1,
                   committed ? "MENU_RESULT_COMPLETES" : marker);
  if (!accepted) requireAdmission(menuSnapshot(session) == before, marker);
  if (committed) {
    requireAdmission(countEvent("child-drop") == 1, "MENU_RESULT_CHILD_DROP");
    const auto size = admissionEvents.size();
    xp_epub_test_menu_result(session.rust, nullptr);
    requireAdmission(admissionEvents.size() == size && after.screenshot, "MENU_RESULT_CONSUMED");
  }
  requireAdmission(!lockDepth && !menuModelDepth, "MENU_CONFIRM_LOCK_BALANCE");
  std::printf("PASS confirm strip=%u committed=%u reject=%u\n", unsigned(XP_STRIP_RENDER), unsigned(committed), rejectAt);
}
int main(int argc, char** argv) {
  if (argc != 3) return 64;
  try { confirmCase(std::string(argv[1]) == "1", unsigned(std::stoul(argv[2]))); }
  catch (const std::exception& error) {
    std::fprintf(stderr, "EXPECTED_ASSERTION: %s\n", error.what());
    return 42;
  }
  return 0;
}
