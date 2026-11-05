static void a1Render(void* owner) {
  assert(owner == xp_app_test_current_epub());
  const auto begin = a1Events.size();
  {
    RenderLock lock;
    XpAppRenderToken token{&lock};
    xp_app_render(&token);
    assert(token.lock == &lock);
  }
  assert(!livePages && !boundSource && !a1Root->frame);
  const auto copy = a1Find("metadata-copy", begin);
  const auto bind = a1Find("bind", copy + 1);
  const auto finish = a1Find("finish-end", bind + 1);
  const auto closed = a1Find("page-close", finish + 1);
  assert(copy < bind && bind < finish && finish < closed);
  { // Metadata is owned by the native root after the unique Page has died.
    RenderLock lock;
    assert(a1Root->metadata.footnotes().size() == 1 && a1Root->metadata.links().size() == 1);
    assert(std::strcmp(a1Root->metadata.footnotes()[0].href, "chapter.xhtml#note") == 0);
    assert(std::strcmp(a1Root->metadata.links()[0].href, "chapter.xhtml#link") == 0);
    uint16_t hit = 0xffff;
    assert(xp_epub_test_link_at_point(owner, 34, 32, 11, 3, &hit) && hit == 0);
  }
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  Fixture appState; fixture = &appState;
  GfxRenderer renderer; MappedInputManager input;
  target = &renderer; mapped = &input; scene.renderer = target; scene.input = mapped;
  testNow = 100;
  SETTINGS.textAntiAliasing = true;
  ESP.free = ESP.block = 100000;
  gpio.x3 = true; BoardConfig::a4 = false; BoardConfig::ACTIVE.board = BoardConfig::Board::Other;
  display.controller = HalDisplay::Controller::UC8253;
  renderer.caps = {HalDisplay::GrayscaleBase::Separate, true, true, false};
  bind(); bindA1(); initializeA1Ble(); initialize(); appState.startAttempt();
  assert(install() == 1);
  assertEntered();
  void* owner = xp_app_test_current_epub(); assert(owner); scene.owner = owner;
  a1Render(owner);
  XpEpubTestProjectionObservation position{};
  xp_epub_test_projection_observe(owner, &position); assert(position.spine == 1 && position.page == 2);

  // The App routes a remote turn through the actual owner and coalesces its Update.
  const auto beforeTurnUpdate = a1Updates;
  XpAppEvent turn{}; turn.kind = XP_APP_REMOTE_TURN; turn.value.remote_turn = {1, 0};
  assert(xp_app_event(&turn) == XP_APP_ACCEPTED); xp_app_test_drain();
  assert(a1Updates > beforeTurnUpdate);
  xp_epub_test_projection_observe(owner, &position); assert(position.page == 3);
  a1Render(owner);

  // Open, paint, and cancel an actual toolbar without a policy replacement.
  xp_epub_test_menu_open(owner); xp_app_test_drain();
  assert(sceneObserve().overlay == 1 && scene.toolbar && !scene.toolbar->ready);
  a1Render(owner);
  assert(scene.toolbar->ready && sceneObserve().stored);
  input.released = 1u << unsigned(MappedInputManager::Button::Back);
  xp_epub_test_loop(owner); xp_app_test_drain(); input = {};
  assert(sceneObserve().overlay == 0 && !scene.toolbar);

  XpAppEvent refresh{}; refresh.kind = XP_APP_FORCED_REFRESH;
  const auto beforeRefresh = a1Updates;
  assert(xp_app_event(&refresh) == XP_APP_ACCEPTED); xp_app_test_drain();
  XpEpubTestBaseObservation base{};
  xp_epub_test_base_observe(owner, &base); assert(base.forced && a1Updates > beforeRefresh);
  a1Render(owner);
  xp_epub_test_base_observe(owner, &base); assert(!base.forced && base.rendered && base.counter == 3);

  const auto beforeFinal = a1Updates;
  XpAppEvent update{}; update.kind = XP_APP_RENDER_REQUESTED; update.value.render_immediate = 0;
  assert(xp_app_event(&update) == XP_APP_ACCEPTED); xp_app_test_drain();
  assert(a1Updates == beforeFinal + 1 && xp_app_test_current_epub() == owner);
  const auto finalUpdate = a1Find("update", a1Events.size() - 1);
  assert(a1Copies == 4 && a1Finishes == 4 && a1Statuses >= 4 && a1Fills > 0 && a1MetadataReads >= 6);
  assert(a1Paints >= 4 && a1Gray > 0 && SETTINGS.textAntiAliasing);
#if XP_STRIP_RENDER
  assert(a1Prepares >= 4 && a1Sources >= 4 && a1Replays >= 4);
#else
  assert(a1Prepares == 0 && a1Sources == 0 && a1Replays == 0);
#endif
  drainToSleep(); scene.owner = nullptr;
  assert(!a1Root && !sceneSection && !livePages && !boundSource);
  const auto rootBegin = a1Find("root-drop-begin", finalUpdate + 1);
  const auto rootEnd = a1Find("root-drop-end", rootBegin + 1);
  assert(finalUpdate < a1Find("section-close", finalUpdate + 1) && rootBegin < rootEnd);
  f().ordered(1, {"exit", "extractor-clear", "section-close", "clippings-unload", "close:book",
                  "close:failure-popup", "close:overlay-popup", "close:bookmarks", "close:root"});
  for (unsigned kind = 0; kind < unsigned(Kind::Legacy); ++kind) f().count(Kind(kind), 1, 1);
  popFinalSleep(); assert(appState.handles.empty()); fixture = nullptr;
  for (const auto& array : arrays) assert(!array.pointer);
  if (a1WrongFont) {
    std::fputs("EXPECTED_ASSERTION: A1_WRONG_FONT\n", stderr); return 42;
  }
  std::printf("A1 PASS strip=%d pages=%u fills=%u status=%u metadata=%u updates=%u\n",
              XP_STRIP_RENDER, a1Finishes, a1Fills, a1Statuses, a1Copies, a1Updates);
  return 0;
}
