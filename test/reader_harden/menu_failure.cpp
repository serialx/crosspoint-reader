int main() {
  using Action = EpubReaderMenuActivity::MenuAction;
  for (const auto action : {Action::SCREENSHOT, Action::TEXT_SETTINGS, Action::GO_HOME}) {
    EpubReaderActivity owner;
    owner.toolbar = false;
    MenuSessionBridge session(owner);
    owner.openReaderMenu();
    assert(owner.child && owner.callback && std::string(owner.child->kind) == "menu");
    ActivityResult result;
    MenuResult menu;
    menu.action = static_cast<int>(action);
    menu.orientation = SETTINGS.orientation;
    menu.pageTurnOption = 0;
    result.data = menu;
    auto callback = std::move(owner.callback);
    owner.child.reset();
    callback(result);
    XpEpubTestMenuObservation after{};
    xp_epub_test_menu_observe(session.rust, &after);
    if (action == Action::SCREENSHOT) assert(after.screenshot);
    if (action == Action::TEXT_SETTINGS) assert(owner.child && std::string(owner.child->kind) == "text");
    assert(!lockDepth && !menuModelDepth);
  }
  puts("PASS unknown percent: classic menu opens; screenshot, text settings and home complete");
}
