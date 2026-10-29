int main() {
  // Mutate every formerly RenderLock-serialized path while rendering is paused
  // first at resource validation/painting, then inside synchronous refresh.
  for (bool painting : {false, true}) {
    LibraryListActivity library;
    library.query = std::string(180, 'q');
    library.applyFilter();
    library.openSearch();
    std::atomic<bool> paused{false}, finish{false};
    std::thread render([&] {
      LibraryListActivity::RenderLock lock(library);
      XpUiScene scene{};
      assert(library.buildRustScene(scene));
      const char* title = library.headerTitle();
      const char* row = library.winTitles[0].c_str();
      const std::string expectedTitle = title, expectedRow = row;
      paused = true;
      while (!finish) std::this_thread::yield();
      if (painting) {
        assert(std::string(title) == expectedTitle);
        assert(std::string(row) == expectedRow);
        assert(std::string(library.headerTitle()) == expectedTitle);
      }
      // Old viewport feedback must not overwrite a newer filter/group transaction.
      LibraryListActivity::ModelLock modelLock(library);
      library.frame.next.list.top = 99;
      library.rustPublished();
    });
    while (!paused) std::this_thread::yield();
    auto polling = std::async(std::launch::async, [&] {
      // Confirm releases are routed by the production effect handler in order.
      for (int tab : {1, 2, 0}) {
        for (bool release : {false, true}) {
          XpUiInput input{};
          input.pressed = release ? 0 : XP_UI_CONFIRM;
          input.released = release ? XP_UI_CONFIRM : 0;
          const auto facts = library.facts(&library.state);
          const auto result = xp_ui_library_update(&library.state, &input, &facts);
          library.applyRustEffect(result.effect);
        }
        assert(library.state.active_tab == tab);
      }
      XpUiInput down{};
      down.pressed = XP_UI_NAV_NEXT;
      const auto facts = library.facts(&library.state);
      library.applyRustEffect(xp_ui_library_update(&library.state, &down, &facts).effect);
      assert(library.state.lists[0].selected == 1);
      XpUiEffect clear{};
      clear.kind = XP_UI_LIBRARY_CLEAR_QUERY;
      library.applyRustEffect(clear);
      library.searchResult(ActivityResult{false, KeyboardResult{std::string(160, 'n')}});
      library.collapseGroups(2);
      library.expandGroup(0);
    });
    const bool ingested = polling.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready;
    finish = true;
    render.join();
    polling.get();
    assert(ingested && "Library input waited for painting or panel refresh");
    assert(library.state.active_tab == 0);
    assert(library.state.lists[0].top != 99);
    assert(library.query == std::string(160, 'n'));
  }
  // Mutations wait for construction's borrowed filter use, then proceed before
  // painting. This is a deterministic overlap test, not firmware timing evidence.
  LibraryListActivity library;
  library.applyFilter();
  building = finishBuild = false;
  auto render = std::async(std::launch::async, [&] {
    XpUiScene s{};
    return library.buildRustScene(s);
  });
  while (!building) std::this_thread::yield();
  auto mutate = std::async(std::launch::async, [&] {
    XpUiEffect effect{};
    effect.kind = XP_UI_LIBRARY_CLEAR_QUERY;
    library.applyRustEffect(effect);
  });
  assert(mutate.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
  finishBuild = true;
  assert(render.get());
  mutate.get();
  XpUiScene stable{};
  assert(library.buildRustScene(stable));
  library.frame.next.list.top = 7;
  library.rustPublished();
  assert(library.state.lists[0].top == 7);
  failBuild = true;
  assert(!library.buildRustScene(stable));
  auto afterFailure = std::async(std::launch::async, [&] {
    XpUiEffect effect{};
    effect.kind = XP_UI_LIBRARY_CLEAR_QUERY;
    library.applyRustEffect(effect);
  });
  assert(afterFailure.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready);
  afterFailure.get();
}
