#include <xp_ui_keyboard.h>
struct BookmarkKeyboard { std::string title, initial; bool locked = false; };
static BookmarkKeyboard* bookmarkKeyboard;
static unsigned keyboardDestroyed, keyboardReleased;
static void bindBookmarkKeyboard() {
  static auto create = legacy.create;
  legacy.create = [](const XpAppLaunch* launch) -> void* {
    if (launch->kind != XP_APP_KEYBOARD) return create(launch);
    const auto& value = *launch->value.keyboard;
    assert(!bookmarkKeyboard && value.maximum == 128 && value.input_type == 0 && value.title.len);
    return bookmarkKeyboard = new BookmarkKeyboard{text(value.title), text(value.initial)};
  };
  static auto enter = legacy.enter;
  legacy.enter = [](void* raw) {
    if (raw != bookmarkKeyboard) return enter(raw);
    assert(!f().renderDepth && !f().modelDepth);
  };
  static auto exit = legacy.exit;
  legacy.exit = [](void* raw) {
    if (raw != bookmarkKeyboard) return exit(raw);
    assert(f().renderDepth == 1 && !f().modelDepth && !bookmarkKeyboard->locked);
  };
  static auto destroy = legacy.destroy;
  legacy.destroy = [](void* raw) {
    if (raw != bookmarkKeyboard) return destroy(raw);
    assert(f().renderDepth == 1 && !f().modelDepth && !xp_app_test_current_child(nullptr));
    delete bookmarkKeyboard; bookmarkKeyboard = nullptr; ++keyboardDestroyed;
  };
  static auto facts = legacy.facts;
  legacy.facts = [](const void* raw, uint8_t a, uint8_t b) -> XpAppLegacyFacts {
    if (raw != bookmarkKeyboard) return facts(raw, a, b);
    XpAppLegacyFacts value{}; value.kind = 255; value.prevent_sleep = 1; return value;
  };
}
extern "C" void xp_keyboard_launch(void* raw, XpAppKeyboardLaunch* out) {
  assert(raw == bookmarkKeyboard); *out = {wire(bookmarkKeyboard->title), wire(bookmarkKeyboard->initial), 128, 0};
}
extern "C" void xp_keyboard_release_launch(void* raw) {
  assert(raw == bookmarkKeyboard); bookmarkKeyboard->title.clear(); bookmarkKeyboard->initial.clear(); ++keyboardReleased;
}
extern "C" XpUiKeyboardLayouts xp_keyboard_layouts() {
  return {nullptr,
    [](void*, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t row) -> XpUiKeyboardRow {
      return {uint8_t(row == 0), 0, 0, 0};
    },
    [](void*, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) -> XpUiKeyboardKey {
      return {"OK", nullptr, nullptr, 13, 1, 2};
    }};
}
extern "C" XpUiKeyboardMetrics xp_keyboard_metrics(void* raw) {
  assert(raw == bookmarkKeyboard);
  return {nullptr,
    [](void*, int32_t, const uint8_t*, uint32_t length, uint8_t mode) -> int32_t { return mode == 2 ? 0 : int32_t(length * 8); },
    [](void*, int32_t) -> int32_t { return 20; }};
}
extern "C" void xp_keyboard_snapshot(void* raw, XpUiKeyboardViewLayout* out, uint16_t* configured, uint8_t* language) {
  assert(raw == bookmarkKeyboard);
  if (out) {
    *out = {}; out->screen = out->content = {0,0,528,792}; out->header_height = 40;
    out->spacing = 8; out->key_spacing = 6; out->key_height = 56; out->hints_height = 40;
    out->text_percent = 80; out->field_normal = 1; out->field_cursor = 2; out->touch = 1;
  }
  if (configured) *configured = 1;
  if (language) *language = 0;
}
extern "C" void xp_keyboard_text_lock(void* raw, uint8_t acquire) {
  assert(raw == bookmarkKeyboard);
  if (acquire) { assert(!f().modelDepth && !bookmarkKeyboard->locked); bookmarkKeyboard->locked = true; }
  else { assert(bookmarkKeyboard->locked); bookmarkKeyboard->locked = false; }
}
extern "C" void xp_keyboard_draw_key(void* raw, const XpUiCommand*, const XpUiKeyboardPaintKey*, uint8_t, uint8_t, uint8_t) {
  assert(raw == bookmarkKeyboard && f().renderDepth == 1);
}
extern "C" void xp_keyboard_chrome(void* raw, const XpUiCommand*, XpUiRect, XpAppText) {
  assert(raw == bookmarkKeyboard && f().renderDepth == 1);
}
extern "C" void xp_keyboard_deliver(const XpAppLegacy*, void*, XpAppText, uint8_t) {
  assert(false && "Rust Bookmarks receives the moved result directly");
}
extern "C" void xp_epub_test_bookmark_rows(void*, XpEpubBookmarkSink, void*);
static std::string bookmarkDisk = R"({"bookmarks":[{"xpath":"target-xpath","percentage":0.375,"summary":"saved passage","name":"named bookmark","si":4,"pc":10,"pp":3,"vo":321}]})";
static bool bookmarkPopup, bookmarkLong, bookmarkWriteFails, bookmarkGrowthFailure;
static int bookmarkChoice = -1;
static unsigned bookmarkOptions, bookmarkWrites, bookmarkPaints;
static size_t bookmarkCommandFailure;
static unsigned bookmarkCommandLogs, bookmarkCommandAttempts;
static bool bookmarkPointPresent;
static uint8_t bookmarkPointKind = 1;
static int32_t bookmarkPointX, bookmarkPointY;
static std::vector<XpUiCommand> bookmarkCommands;
static void (*bookmarkChoose)(void*, int32_t);
static void* bookmarkChoiceContext;
static std::string bookmarkPath() {
  return "/.crosspoint/bookmarks/" + f().path.substr(1, f().path.size() - 6) + ".json";
}
static void bindOwnedBookmarks() {
  bindBookmarkKeyboard();
  static auto logEnabled = leaves.log_enabled;
  leaves.log_enabled = [](uint8_t level) -> uint8_t {
    return level == 1 && bookmarkCommandFailure ? 1 : logEnabled(level);
  };
  static auto allocate = resources.buffer_allocate;
  resources.buffer_allocate = [](uint8_t domain, size_t bytes) -> uint8_t* {
    if (bookmarkCommandFailure == bytes) { ++bookmarkCommandAttempts; return nullptr; }
    return allocate(domain, bytes);
  };
  static auto log = leaves.log;
  leaves.log = [](uint8_t level, XpAppText origin, XpAppText message) {
    const auto tag = text(origin), value = text(message);
    if (tag == "RUI") {
      assert(level == 1 && bookmarkCommandFailure &&
             value == "Cannot allocate " + std::to_string(bookmarkCommandFailure) + " command bytes");
      ++bookmarkCommandLogs; return;
    }
    if (tag == "BKM") {
      assert(level == 0 && (value.starts_with("Loaded ") || value.starts_with("Saving "))); return;
    }
    if (tag == "EPB") {
      if (level == 0) assert(value.starts_with("Loaded "));
      else assert(bookmarkWriteFails && (value == "Failed to save bookmarks after delete" ||
                                       value == "Failed to save bookmarks after rename"));
      return;
    }
    if (tag == "PERSIST") {
      if (bookmarkGrowthFailure) {
        assert(level == 1 && value == "JSON parse error in " + bookmarkPath() + ": NoMemory"); return;
      }
      assert(level == 1 && bookmarkWriteFails && value == "Failed to write " + bookmarkPath()); return;
    }
    if (tag == "LIST") { assert(level == 0 && value.starts_with("Selected ")); return; }
    log(level, origin, message);
  };
  leaves.storage_read_file = [](XpAppText path, XpAppTextSink sink, void* context) -> uint8_t {
    assert(!f().modelDepth && text(path) == bookmarkPath());
    ++f().bookmarkLoads; f().physical("bookmarks-load", f().extractor->generation, f().extractor->id);
    // The inherited reader-entry fixture starts with an empty cache; the child reloads the disk.
    if (bookmarkGrowthFailure) {
      const auto before = xp_test_live_allocations(); xp_test_fail_allocation_after(0);
      const auto accepted = sink(context, wire(bookmarkDisk));
      const auto attempts = xp_test_allocation_attempts(); xp_test_fail_allocation_after(SIZE_MAX);
      assert(accepted && attempts == 1 && xp_test_live_allocations() == before);
      return accepted;
    }
    return sink(context, f().bookmarkLoads == 1 ? literal("[]") : wire(bookmarkDisk));
  };
  static auto mkdir = leaves.path_mkdir;
  leaves.path_mkdir = [](XpAppText path) -> uint8_t {
    assert(!f().modelDepth);
    if (text(path) == "/.crosspoint/bookmarks/" || text(path) == "/.crosspoint") return 1;
    return mkdir(path);
  };
  leaves.storage_write_file_atomic = [](XpAppText path, const uint8_t* bytes, uint32_t length) -> uint8_t {
    assert(f().renderDepth == 1 && !f().modelDepth && text(path) == bookmarkPath());
    ++bookmarkWrites;
    if (bookmarkWriteFails) return 0;
    bookmarkDisk.assign(reinterpret_cast<const char*>(bytes), length); return 1;
  };
  static auto open = leaves.popup_open;
  leaves.popup_open = []() -> void* {
    if (f().componentAttempt < 3) return open();
    return f().acquire(Kind::OverlayPopup, 0);
  };
  leaves.popup_show = [](void*, XpAppText, XpAppText message, uint32_t count, int32_t selected,
      uint8_t (*label)(void*, uint32_t, XpAppTextSink, void*), void* labels,
      void (*choose)(void*, int32_t), void* context) {
    assert(f().renderDepth == 1 && !message.len && (count == 2 || count == 3) && selected == 0 && !bookmarkPopup);
    for (uint32_t i = 0; i < count; ++i)
      assert(label(labels, i, [](void*, XpAppText value) -> uint8_t { assert(value.len); return 1; }, nullptr));
    bookmarkOptions = count; bookmarkPopup = true; bookmarkChoose = choose; bookmarkChoiceContext = context;
  };
  leaves.popup_active = [](void*) -> uint8_t { return bookmarkPopup; };
  leaves.popup_input = [](void*, XpEpubPopupInput* out) {
    *out = {}; out->consumed = bookmarkPopup;
    if (bookmarkPopup && bookmarkChoice >= 0) {
      bookmarkChoose(bookmarkChoiceContext, bookmarkChoice); bookmarkChoice = -1;
      bookmarkPopup = false; out->changed = 1;
    }
  };
  leaves.popup_process_render = [](void*) { assert(f().renderDepth == 1 && bookmarkPopup && bookmarkPaints); };
  leaves.child_list_help = [](XpUiRect, XpAppText value) { assert(f().renderDepth == 1 && value.len); };
  leaves.child_list_icon = [](uint8_t kind, XpUiRect, XpUiRect, uint8_t paint) {
    assert(f().renderDepth == 1 && kind <= 1 && paint <= 1);
  };
  leaves.input_screen_point = [](uint8_t kind, int32_t* x, int32_t* y) -> uint8_t {
    assert(kind <= 1);
    if (!bookmarkPointPresent || kind != bookmarkPointKind) return 0;
    *x = bookmarkPointX; *y = bookmarkPointY; return 1;
  };
  leaves.input_services = []() -> const XpUiReaderInputServices* {
    static const XpUiReaderInputServices service{
      nullptr,
      [](void*, uint32_t button, uint8_t, uint32_t duration) -> uint8_t {
        assert(button == XP_UI_CONFIRM && duration == 700); return bookmarkLong;
      },
      [](void*, uint8_t) -> uint32_t { return 1; },
      [](void*, int32_t*, int32_t*) -> uint8_t { return 0; }
    };
    return &service;
  };
  static auto paint = resources.paint_scene;
  resources.paint_scene = [](const XpUiScene* scene, const XpUiPaint* resources) -> uint8_t {
    bookmarkCommands.assign(scene->commands, scene->commands + scene->command_count);
    ++bookmarkPaints; return paint(scene, resources);
  };
}
static void bookmarkPointGesture(const std::string& mode, bool cancelled) {
  const auto row = std::find_if(bookmarkCommands.begin(), bookmarkCommands.end(), [](const auto& hit) {
    return hit.kind == XP_UI_TEXT_HANDLE && hit.resource == 5;
  });
  assert(row != bookmarkCommands.end());
  bookmarkPointX = row->rect.x + row->rect.w / 2;
  bookmarkPointY = row->rect.y + row->rect.h / 2;
  bookmarkPointPresent = mode != "bookmarks-swipe-absent";
  if (mode == "bookmarks-swipe-outside") bookmarkPointY = -100;
  bookmarkPointKind = mode == "bookmarks-touch-hold" ? 0 : 1;
  childInput = {}; childInput.input.now_ms = 100;
  childInput.input.touch_x = childInput.input.touch_y = -1;
  childInput.input.swipe = bookmarkPointKind ? XP_UI_SWIPE_LEFT : XP_UI_SWIPE_NONE;
  xp_app_test_activity_loop(); bookmarkPointPresent = false;
  if (mode == "bookmarks-touch-hold") {
    assert(bookmarkPopup && bookmarkOptions == 3);
    bookmarkChoice = 2; inputChild(0);
  } else {
    paintChild();
    const auto remove = std::find_if(bookmarkCommands.begin(), bookmarkCommands.end(), [](const auto& hit) {
      return hit.kind == XP_UI_BITMAP && hit.resource == 0x80000008;
    });
    if (mode != "bookmarks-swipe") {
      assert(remove == bookmarkCommands.end() && !bookmarkPopup && !bookmarkWrites);
      returnChild(XP_UI_BACK); return;
    }
    assert(remove != bookmarkCommands.end());
    childInput = {}; childInput.input.now_ms = 200; childInput.input.touch_flags = XP_UI_TOUCH_RELEASED;
    childInput.input.touch_x = remove->rect.x + remove->rect.w / 2;
    childInput.input.touch_y = remove->rect.y + remove->rect.h / 2;
    xp_app_test_activity_loop();
  }
  assert(bookmarkPopup && bookmarkOptions == 2);
  bookmarkChoice = cancelled ? 0 : 1; inputChild(0);
  assert(bookmarkWrites == unsigned(!cancelled));
  assert(bookmarkDisk.find("named bookmark") != std::string::npos);
  assert((bookmarkDisk.find("second row") != std::string::npos) == cancelled);
  paintChild(); returnChild(XP_UI_BACK);
}
static void renameBookmark(bool cancelled, const std::string& expected) {
  const auto* bookmarkOwner = xp_app_test_current_child(nullptr);
  const auto beforeBuffers = readerBuffers;
  const uint8_t* originalName = nullptr;
  xp_epub_test_bookmark_rows(results.owner, [](void* raw, const XpEpubBookmark* row) -> uint8_t {
    *static_cast<const uint8_t**>(raw) = row->name.data; return 1;
  }, &originalName);
  const auto destroyed = keyboardDestroyed, released = keyboardReleased, writes = bookmarkWrites;
  bookmarkLong = true; inputChild(0); bookmarkLong = false;
  assert(bookmarkPopup && bookmarkOptions == 3);
  bookmarkChoice = 1; inputChild(0);
  assert(bookmarkKeyboard && xp_app_test_parent_count() == 2 && !xp_app_test_current_child(nullptr));
  assert(keyboardReleased == released + 1 && bookmarkKeyboard->title.empty() && bookmarkKeyboard->initial.empty());
  const uint8_t* edit = nullptr;
  for (auto [pointer, length] : readerBuffers) {
    if (!beforeBuffers.contains(pointer) && length == expected.size() + 1 && std::memcmp(pointer, expected.c_str(), length) == 0) {
      assert(!edit); edit = static_cast<const uint8_t*>(pointer);
    }
  }
  assert(edit);
  void* lock = runtime.render_lock(); XpAppRenderToken token{lock}; xp_app_render(&token); runtime.render_unlock(lock);
  childInput = {}; childInput.input.now_ms = 100;
  childInput.input.pressed = childInput.input.released = cancelled ? XP_UI_BACK : XP_UI_CONFIRM;
  xp_app_test_activity_loop();
  assert(!bookmarkKeyboard && keyboardDestroyed == destroyed + 1 && xp_app_test_parent_count() == 1);
  assert(xp_app_test_current_child(nullptr) == bookmarkOwner && !f().renderDepth && !f().modelDepth);
  assert(bookmarkWrites == writes + unsigned(!cancelled));
  if (!cancelled) {
    struct Name { const uint8_t* pointer; const std::string* expected; unsigned count = 0; } name{bookmarkWriteFails ? originalName : edit, &expected};
    xp_epub_test_bookmark_rows(results.owner, [](void* raw, const XpEpubBookmark* row) -> uint8_t {
      auto& name = *static_cast<Name*>(raw); assert(name.count++ == 0);
      assert(row->name.data == name.pointer && text(row->name) == *name.expected); return 1;
    }, &name);
    assert(name.count == 1);
  }
  if (cancelled || bookmarkWriteFails) assert(!readerBuffers.contains(const_cast<uint8_t*>(edit)));
  paintChild();
}

static int runOwnedBookmarks(const std::string& mode, bool cancelled) {
  const auto baseline = xp_test_live_allocations();
  bookmarkGrowthFailure = mode == "bookmarks-record-oom";
  bookmarkWriteFails = mode == "bookmarks-rename-fail";
  if (mode == "bookmarks-oom") {
    xp_test_fail_allocation_after(0); xp_epub_test_menu_confirm(results.owner, 9);
    const auto attempts = xp_test_allocation_attempts(); xp_test_fail_allocation_after(SIZE_MAX);
    assert(attempts == 1 && !xp_app_test_pending_kind() && xp_app_test_current_epub() == results.owner);
    assert(xp_test_live_allocations() == baseline);
  } else {
    std::string renameName = "named bookmark";
    if (mode == "bookmarks-rename-empty") renameName.clear();
    if (mode == "bookmarks-rename-max") renameName.assign(128, 'x');
    if (mode.starts_with("bookmarks-rename")) {
      const auto start = bookmarkDisk.find("named bookmark"); assert(start != std::string::npos);
      bookmarkDisk.replace(start, std::string("named bookmark").size(), renameName);
    }
    if (mode == "bookmarks-empty") bookmarkDisk = "{\"bookmarks\":[]}";
    if (mode == "bookmarks-command-oom") bookmarkCommandFailure = 40 * sizeof(XpUiCommand);
    if (mode.starts_with("bookmarks-swipe") || mode == "bookmarks-touch-hold") {
      const auto end = bookmarkDisk.rfind("]}"); assert(end != std::string::npos);
      bookmarkDisk.insert(end, R"(,{"name":"second row","si":4,"pc":10,"pp":5})");
    }
    for (unsigned visit = 0; visit < (mode == "bookmarks" ? 3u : 1u); ++visit) {
      xp_epub_test_menu_confirm(results.owner, 9);
      assert(xp_app_test_pending_kind() == 1);
      if (mode == "bookmarks-stack-oom") { results.allocatingStack = true; xp_test_fail_allocation_after(0); }
      assert(xp_app_test_drain());
      if (mode == "bookmarks-stack-oom") {
        const auto attempts = xp_test_allocation_attempts(); xp_test_fail_allocation_after(SIZE_MAX); results.allocatingStack = false;
        assert(attempts == 1 && !xp_app_test_current_child(nullptr) && !xp_app_test_parent_count());
        assert(xp_app_test_current_epub() == results.owner && xp_test_live_allocations() == baseline);
        break;
      }
      if (mode == "bookmarks-command-oom") {
        assert(bookmarkCommandAttempts == 1 && bookmarkCommandLogs == 1 && !bookmarkWrites);
        bookmarkCommandFailure = 0;
        assert(childKind() == 1); paintChild(); returnChild(XP_UI_BACK);
        assert(xp_app_test_current_epub() == results.owner && !xp_app_test_parent_count());
        break;
      }
      assert(childKind() == 8);
      if (mode.starts_with("bookmarks-swipe") || mode == "bookmarks-touch-hold") {
        paintChild();
        bookmarkPointGesture(mode, cancelled);
      } else if (mode.starts_with("bookmarks-rename")) {
        paintChild();
        for (unsigned rename = 0; rename < 3; ++rename) renameBookmark(cancelled, renameName);
        returnChild(XP_UI_BACK);
      } else if (mode == "bookmarks-delete" || mode == "bookmarks-delete-fail") {
        paintChild();
        bookmarkLong = true; inputChild(0); bookmarkLong = false;
        assert(bookmarkPopup && bookmarkOptions == 3);
        const auto before = bookmarkPaints; paintChild(); assert(bookmarkPaints == before + 1);
        bookmarkChoice = 2; inputChild(0); assert(bookmarkPopup && bookmarkOptions == 2);
        bookmarkChoice = cancelled ? 0 : 1;
        bookmarkWriteFails = mode == "bookmarks-delete-fail";
        if (cancelled) { inputChild(0); assert(!bookmarkWrites); returnChild(XP_UI_BACK); }
        else { returnChild(0); assert(bookmarkWrites == 1); }
        assert((bookmarkDisk == "{\"bookmarks\":[]}") == (!cancelled && !bookmarkWriteFails));
      } else {
        paintChild();
        returnChild(cancelled || mode == "bookmarks-empty" || bookmarkGrowthFailure ? XP_UI_BACK : XP_UI_CONFIRM);
        if (!cancelled && mode == "bookmarks") {
          XpEpubTestMenuObservation observation{}; xp_epub_test_menu_observe(results.owner, &observation);
          assert(observation.spine == 4 && observation.next_page == 3);
        }
      }
      if (xp_app_test_current_child(nullptr)) { assert(childKind() == 1); paintChild(); returnChild(XP_UI_BACK); }
      assert(xp_app_test_current_epub() == results.owner && !xp_epub_test_has_child(results.owner));
    }
  }
  closeToSleep();
  assert(readerFiles.empty() && readerFacades.empty() && readerBuffers.empty());
  assert(readerSectionOpens == readerSectionCloses && f().handles.empty());
  assert(results.children.empty() && results.born == results.dead);
  assert(clipping_fixture::readerLiveAllocations() == (mode == "bookmarks-oom" || mode == "bookmarks-stack-oom" ? 0u : 1u));
  assert(activeChildMarker.empty() && !f().renderDepth && !f().modelDepth);
  std::printf("owned App %s cancelled=%u: moves, popup underlay, persistence and teardown checked\n", mode.c_str(), cancelled);
  return 0;
}
