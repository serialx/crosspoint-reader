static unsigned events(const char* prefix) {
  return std::count_if(transcript.begin(), transcript.end(), [=](const auto& s) { return s.starts_with(prefix); });
}
static void repaint(Candidate& a) {
  a.render(RenderLock{});
  assert(!a.rustSceneRejected());
}
int main(int argc, char** argv) {
  assert(argc == 3);
  const std::string which = argv[1];
  const int rejection = atoi(argv[2]); // 0: renderer alone, 1: persistent, 2: immediate recovery.
  HalDisplay display;
  GfxRenderer renderer(display);
  renderer.begin();
  static EpdFont small(&ubuntu_10_regular), smallBold(&ubuntu_10_bold), body(&ubuntu_12_regular), bodyBold(&ubuntu_12_bold);
  renderer.insertFont(UI_10_FONT_ID, EpdFontFamily(&small, &smallBold));
  renderer.insertFont(UI_12_FONT_ID, EpdFontFamily(&body, &bodyBold));
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts(), renderer.getTtfFonts());
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  safe = {3, 5, renderer.getScreenWidth() - 8, renderer.getScreenHeight() - 45};
  MappedInputManager input;
  Candidate a(renderer, input, false);
  if (which.starts_with("cancel")) {
    const bool firstFrame = which.find("first") != std::string::npos;
    const bool backOnly = which.find("back") != std::string::npos;
    a.model.phase = XP_UI_WIFI_PHASE_SCANNING;
    a.model.flash_index = -1;
    xp_ui_settings_list_init(&a.model.list, 0);
    if (!firstFrame) repaint(a);
    bool interrupted = false;
    scanDeleteHook = [&] {
      assert(!interrupted);
      interrupted = true;
      if (firstFrame && rejection) {
        assert(!a.commands && a.commandCapacity == 0);
        failAllocation = 0;
        a.render(RenderLock{});
        assert(a.rustSceneRejected() && !a.commands && a.commandCapacity == 0);
        failAllocation = -1;
      } else if (rejection) {
        a.rejectRustScene();
      }
      if (rejection == 2) repaint(a);
    };
    input.pressed = XP_UI_BACK;
    input.held = XP_UI_BACK | (backOnly ? 0 : XP_UI_NAV_NEXT);
    a.loop();
    scanDeleteHook = {};
    assert(interrupted);
    // A finished activity is removed by the manager before the next input tick.
    input = {};
    if (!a.finished) a.loop();
    assert(events("result:cancelled") == 1 && events("finish") == 1);
    assert(events("result:connected") == 0 && events("scan-delete") == 1);
    assert(a.model.count == 0 && a.networks.empty() && a.networkStatuses.empty());
    assert(events("scan-complete") == 0 && events("scan-start:") == 0);
    assert(events("keyboard:") == 0 && events("wifi-begin:") == 0);
    puts(argv[1]);
    return 0;
  }
  a.model.phase = which == "save" ? XP_UI_WIFI_PHASE_SAVE : which == "forget" ? XP_UI_WIFI_PHASE_FORGET : XP_UI_WIFI_PHASE_LIST;
  a.model.count = 1;
  a.model.flash_index = -1;
  a.model.forget_selection = 1;
  a.selectedSSID = "network";
  a.enteredPassword = "secret";
  xp_ui_settings_list_init(&a.model.list, 0);
  a.networks.push_back({"network", -45, which != "connect", which == "forget", which == "hidden"});
  a.networkStatuses.push_back("||||");
  repaint(a);
  bool interrupted = false, boundaryPaintSucceeded = false;
  auto interrupt = [&] {
    interrupted = true;
    // The render occurs after the production resource lock or publisher returns.
    assert(a.resourceMutex->mutex.try_lock());
    a.resourceMutex->mutex.unlock();
    a.render(RenderLock{});
    boundaryPaintSucceeded = !a.rustSceneRejected();
    if (rejection) a.rejectRustScene();
    if (rejection == 2) repaint(a);
  };
  if (which == "save" || which == "forget") {
    unlockHook = [&] {
      if (!interrupted && events(which == "save" ? "persist-add:" : "persist-remove:")) interrupt();
    };
    input.pressed = input.held = XP_UI_CONFIRM;
  } else if (which == "scan") {
    unlockHook = [&] {
      if (!interrupted && a.networks.empty()) interrupt();
    };
    input.pressed = input.held = XP_UI_RIGHT | XP_UI_CONFIRM;
  } else {
    publishHook = [&](Candidate& current) {
      const auto expected = which == "hidden" ? XP_UI_WIFI_PHASE_HIDDEN_SSID :
                            which == "password" ? XP_UI_WIFI_PHASE_PASSWORD : XP_UI_WIFI_PHASE_CONNECTING;
      if (!interrupted && current.model.phase == expected) interrupt();
    };
    input.pressed = input.held = XP_UI_CONFIRM;
    a.loop();
    input.pressed = input.held = 0;
    input.released = XP_UI_CONFIRM;
  }
  a.loop();
  unlockHook = {};
  publishHook = {};
  assert(interrupted);
  if (which == "save") {
    assert(events("persist-add:") == 1 && events("finish") == 1);
  } else if (which == "forget") {
    assert(events("persist-remove:") == 1 && events("scan-start:") == 1);
    assert(a.model.phase == XP_UI_WIFI_PHASE_SCANNING && a.model.count == 0);
    assert(a.networks.empty() && a.networkStatuses.empty());
    repaint(a);
  } else if (which == "scan") {
    assert(a.model.phase == XP_UI_WIFI_PHASE_SCANNING && a.model.count == 0);
    assert(a.networks.empty() && a.networkStatuses.empty());
    assert(events("scan-start:") == 1);
    assert(boundaryPaintSucceeded);
    repaint(a);
    if (!rejection) input = {};
    // Finish the scan through the real runner. Never derive model.count in the fixture.
    WiFi.scan = 1;
    WiFi.rows = {{"new-network", -40, 1}};
    input.pressed = 0;
    a.loop();
    assert(a.model.phase == XP_UI_WIFI_PHASE_LIST && a.model.count == 2);
    assert(a.model.count == a.networks.size() && a.networkStatuses.size() == a.networks.size());
    repaint(a);
    input.held = 0;
    input.released = rejection ? XP_UI_CONFIRM | XP_UI_RIGHT : 0;
    a.loop();
    assert(events("keyboard:") == 0 && events("wifi-begin:") == 0);
    input = {};
    a.loop();
    input.pressed = input.held = XP_UI_RIGHT;
    a.loop();
    assert(events("scan-start:") == 2);
    input = {};
    a.loop();
    repaint(a);
    input.pressed = input.held = XP_UI_CONFIRM;
    a.loop();
    input.pressed = input.held = 0;
    input.released = XP_UI_CONFIRM;
    a.loop();
    assert(events("keyboard:") == 1 && events("wifi-begin:") == 0);
  } else if (which == "connect") {
    assert(a.model.phase == XP_UI_WIFI_PHASE_CONNECTING);
    assert(events("wifi-begin:") == 1);
    repaint(a);
    input = {};
    a.loop();
    assert(events("wifi-begin:") == 1);
  } else {
    assert(events("keyboard:") == 1 && events("wifi-begin:") == 0);
    assert(childResult);
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    childResult(cancelled);
    input = {};
    a.loop();
    assert(a.model.phase == XP_UI_WIFI_PHASE_LIST);
    repaint(a);
    assert(events("wifi-begin:") == 0);
  }
  puts(argv[1]);
}
