#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "ActivityResult.h"
#include "ActivityResultView.h"
#include "app_leaves.h"

bool denyNative = false;
size_t nativeAttempts = 0;
void* operator new(size_t bytes) {
  if (denyNative) {
    ++nativeAttempts;
    std::abort();
  }
  if (auto* p = std::malloc(bytes)) return p;
  std::abort();
}
void operator delete(void* p) noexcept { std::free(p); }
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
extern "C" void xp_test_fail_allocation_after(size_t);
extern "C" size_t xp_test_allocation_attempts();

#define LOG_ERR(...) ((void)0)
#define FREEINK_CAP_USB_MSC 0
struct GfxRenderer {};
struct MappedInputManager {};
int created = -1, flag = -1, destroyed = 0;
struct Activity {
  ActivityResult result;
  ActivityResultHandler resultHandler;
  virtual ~Activity() { ++destroyed; }
};
struct RustActivityAdapter : Activity {
  RustActivityAdapter(const char*, GfxRenderer&, MappedInputManager&) {}
};
struct FileBrowserActivity : RustActivityAdapter {
  enum class Mode { Browse };
  Mode mode;
  std::string basepath;
  FileBrowserActivity(GfxRenderer&, MappedInputManager&, std::string, Mode = Mode::Browse);
};
// FILE_CONSTRUCTOR

template <int Kind>
struct Stub : Activity {
  template <class... Args>
  Stub(GfxRenderer&, MappedInputManager&, Args&&...) {
    created = Kind;
  }
};
using LibraryListActivity = Stub<XP_APP_LIBRARY>;
struct OpdsBookBrowserActivity : Activity {
  OpdsBookBrowserActivity(GfxRenderer&, MappedInputManager&, int server) {
    created = XP_APP_OPDS;
    flag = server;
  }
};
using OpdsServerListActivity = Stub<XP_APP_OPDS_PICKER>;
using BootActivity = Stub<XP_APP_BOOT>;
using SleepActivity = Stub<XP_APP_SLEEP>;
using FullScreenMessageActivity = Stub<XP_APP_MESSAGE>;
using CrashActivity = Stub<XP_APP_CRASH>;
using FrontlightPanelActivity = Stub<XP_APP_FRONTLIGHT_PANEL>;
using BmpViewerActivity = Stub<XP_APP_BITMAP>;
struct ReaderActivity : Stub<XP_APP_READER> {
  using Stub::Stub;
  static std::unique_ptr<Activity> create(GfxRenderer& r, MappedInputManager& i, std::string p, bool fast) {
    return std::make_unique<ReaderActivity>(r, i, p, fast);
  }
};
struct PluginCatalogActivity : Activity {
  PluginCatalogActivity(GfxRenderer&, MappedInputManager&, bool opds, bool show) {
    created = XP_APP_PLUGINS;
    flag = opds;
    assert(show);
  }
};
struct CrossPointWebServerActivity : Activity {
  CrossPointWebServerActivity(GfxRenderer&, MappedInputManager&, bool join) {
    created = XP_APP_FILE_TRANSFER;
    flag = join;
  }
};
enum class SettingsSection : uint8_t { End = 6 };
struct SettingsListActivity : Activity {
  SettingsListActivity(GfxRenderer&, MappedInputManager&, SettingsSection section) {
    created = XP_APP_SETTINGS;
    flag = int(section);
  }
};
struct EpdFontFamily {
  enum class Style {};
};
struct Manager {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
} activityManager;
struct Servers {
  std::vector<int> servers;
  const auto& getServers() const { return servers; }
} OPDS_STORE;
template <class T, class... Args>
auto makeUniqueNoThrow(Args&&... args) {
  if constexpr (std::is_same_v<T, FileBrowserActivity>) {
    assert(std::get<2>(std::forward_as_tuple(args...)).empty());
  }
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
// LEGACY_OWNER
// OWNED_TEXT
static XpAppLegacy legacy = appTestLegacy();
static XpAppResources resources = appTestResources();
static XpAppRuntime runtime = appTestRuntime();
static XpAppCompatibility compatibility;
bool locked = false, renderOnUnlock = false;
int paints = 0, deliveries = 0, diagnostics = 0;
XpUiInput input{};
XpAppHomeSettings settings{};
int books = 0, menu = 1;
XpUiRect menuRect{};
XpAppText text(const char* p) { return {reinterpret_cast<const uint8_t*>(p), std::strlen(p)}; }
void event(XpAppEvent e) { assert(xp_app_event(&e) == XP_APP_ACCEPTED); }
void pop() { event({.kind = XP_APP_POP}); }
void adopt(Activity* activity, bool push) {
  XpAppEvent e{.kind = static_cast<uint8_t>(push ? XP_APP_ADOPT_PUSH : XP_APP_ADOPT_REPLACE)};
  e.value.adopt_replace = ownLegacy(std::unique_ptr<Activity>(activity)).release();
  event(e);
}
void paint() {
  XpAppRenderToken token{};
  xp_app_render(&token);
}
static void checkBorrowedText(XpAppText text, const std::string& native) {
  assert(text.data == reinterpret_cast<const uint8_t*>(native.data()));
  assert(text.len == native.size());
}
static unsigned resultViewChecks[13][2]{};
static void checkResultView(const XpAppResultView& view, const ActivityResult& native) {
  static_assert(std::variant_size_v<ResultVariant> == 13);
  assert(view.original == &native);
  assert(view.kind == native.data.index());
  assert(view.cancelled == native.isCancelled);
  switch (static_cast<ActivityResultKind>(view.kind)) {
    case ActivityResultKind::None:
      assert(std::holds_alternative<std::monostate>(native.data));
      break;
    case ActivityResultKind::Wifi: {
      const auto& value = std::get<WifiResult>(native.data);
      checkBorrowedText(view.value.wifi.ssid, value.ssid);
      checkBorrowedText(view.value.wifi.ip, value.ip);
      assert(view.value.wifi.connected == value.connected);
      break;
    }
    case ActivityResultKind::Keyboard:
      checkBorrowedText(view.value.text, std::get<KeyboardResult>(native.data).text);
      break;
    case ActivityResultKind::Menu: {
      const auto& value = std::get<MenuResult>(native.data);
      assert(view.value.menu.action == value.action);
      assert(view.value.menu.orientation == value.orientation);
      assert(view.value.menu.page_turn_option == value.pageTurnOption);
      break;
    }
    case ActivityResultKind::Chapter: {
      const auto& value = std::get<ChapterResult>(native.data);
      assert(view.value.chapter.spine == value.spineIndex);
      checkBorrowedText(view.value.chapter.anchor, value.anchor);
      break;
    }
    case ActivityResultKind::Percent:
      assert(view.value.signed_value == std::get<PercentResult>(native.data).percent);
      break;
    case ActivityResultKind::Interval:
      assert(view.value.unsigned_value == std::get<IntervalResult>(native.data).value);
      break;
    case ActivityResultKind::Page:
      assert(view.value.unsigned_value == std::get<PageResult>(native.data).page);
      break;
    case ActivityResultKind::Clipping: {
      const auto& value = std::get<ClippingResult>(native.data);
      checkBorrowedText(view.value.clipping.text.bytes, value.text);
      assert(view.value.clipping.text.native_string == &value.text);
      assert(view.value.clipping.start_page_offset == value.startPageOffset);
      assert(view.value.clipping.end_page_offset == value.endPageOffset);
      assert(view.value.clipping.start_word == value.startWordIndex);
      assert(view.value.clipping.end_word == value.endWordIndex);
      assert(view.value.clipping.word_count == value.wordCount);
      assert(view.value.clipping.start_offset == value.startOffset);
      assert(view.value.clipping.end_offset == value.endOffset);
      assert(view.value.clipping.action == static_cast<uint8_t>(value.action));
      break;
    }
    case ActivityResultKind::Progress: {
      const auto& value = std::get<ProgressChangeResult>(native.data);
      assert(view.value.progress.spine == value.spineIndex);
      assert(view.value.progress.page == value.page);
      assert(view.value.progress.pages == value.totalPages);
      checkBorrowedText(view.value.progress.xpath, value.xpath);
      assert(std::memcmp(&view.value.progress.percentage, &value.percentage, sizeof(float)) == 0);
      assert(view.value.progress.offset == value.visibleTextOffset);
      assert(view.value.progress.has_saved_progress == value.hasSavedProgress);
      assert(view.value.progress.has_offset == value.hasVisibleTextOffset);
      break;
    }
    case ActivityResultKind::NetworkMode:
      assert(view.value.signed_value == static_cast<int32_t>(std::get<NetworkModeResult>(native.data).mode));
      break;
    case ActivityResultKind::Footnote:
      checkBorrowedText(view.value.text, std::get<FootnoteResult>(native.data).href);
      break;
    case ActivityResultKind::FilePath:
      checkBorrowedText(view.value.text, std::get<FilePathResult>(native.data).path);
      break;
  }
  ++resultViewChecks[view.kind][view.cancelled];
}
void init() {
  legacy.create =  // FACTORY
      ;
  legacy.destroy = [](void* p) {
    assert(locked);
    delete static_cast<OwnedLegacyActivity*>(p);
  };
  static const auto withNativeResult = +  // WITH_RESULT
      ;
  legacy.with_result = [](void* activity, XpAppResultSink sink, void* context) {
    const auto& original = nativeActivity(activity)->result;
    struct CheckContext {
      XpAppResultSink sink;
      void* context;
      size_t kind;
      bool cancelled;
      bool called;
    } checked{sink, context, original.data.index(), original.isCancelled, false};
    withNativeResult(
        activity,
        [](void* opaque, const XpAppResultView* view) {
          auto& checked = *static_cast<CheckContext*>(opaque);
          assert(view && view->original && !checked.called);
          const auto& native = *static_cast<const ActivityResult*>(view->original);
          assert(native.data.index() == checked.kind && native.isCancelled == checked.cancelled);
          checkResultView(*view, native);
          checked.called = true;
          checked.sink(checked.context, view);
        },
        &checked);
    assert(checked.called);
  };
  legacy.deliver_result =  // DELIVERY
      ;
  legacy.render = [](void*, XpAppRenderToken*) { ++paints; };
  runtime.render_lock = []() -> void* {
    assert(!locked);
    locked = true;
    return &locked;
  };
  runtime.render_try_lock = runtime.render_lock;
  runtime.render_unlock = [](void*) {
    assert(locked);
    locked = false;
    if (renderOnUnlock) paint();
  };
  runtime.ble_before_screen_change = [](uint8_t) -> uint8_t { return 1; };
  runtime.diagnostic = [](uint8_t code, uint32_t, uint32_t) {
    if (code == 10) ++diagnostics;
  };
  runtime.input_mapped_snapshot = [](uint8_t, uint8_t, uint8_t, XpAppMappedSnapshot* out) {
    *out = {};
    out->input = input;
  };
  runtime.display_geometry = []() -> XpAppDisplayGeometry { return {480, 800, 0, 0}; };
  resources.settings_snapshot = [] { return settings; };
  resources.opds_count = []() -> uint32_t { return OPDS_STORE.servers.size(); };
  resources.path_exists = [](XpAppText) -> uint8_t { return 1; };
  resources.recents_visit = [](XpAppBookSink sink, void* context) -> uint8_t {
    if (!books) return 1;
    XpAppBook book{text("/one.epub"), text("Book"), text("Author"), text("")};
    return sink(context, &book);
  };
  resources.translate = [](uint16_t) { return text("label"); };
  resources.text_width = [](XpAppText t, int32_t, uint8_t) -> int32_t { return t.len * 6; };
  resources.line_height = [](int32_t) -> int32_t { return 20; };
  resources.text_wrap = [](XpAppText t, XpAppTextLayout, XpAppTextSink sink, void* ctx) -> uint8_t {
    return sink(ctx, t);
  };
  resources.text_truncate = resources.text_wrap;
  resources.text_box_height = [](XpAppText, XpAppTextLayout) -> int32_t { return 20; };
  resources.theme_snapshot = [](XpAppTheme* out) {
    *out = {};
    out->screen = {0, 0, 480, 800};
    out->content = {6, 0, 468, 800};
    out->safe = out->screen;
    out->title_font = 12;
    out->small_font = 10;
    out->tiny_font = 8;
    out->body_font = 12;
    out->top_padding = 10;
    out->header_height = 50;
    out->home_top = 60;
    out->tile_height = 250;
    out->cover_height = 180;
    out->side_padding = 20;
    out->spacing = 10;
    out->menu_top = 12;
    out->menu_height = 42;
    out->menu_gap = 6;
    out->hints_height = 40;
    out->scroll_width = 4;
    out->scroll_offset = 6;
    out->columns = 3;
    out->tab_height = 40;
    out->tab_height_pct = 100;
    out->tab_width_pct = 100;
  };
  resources.paint_scene = [](const XpUiScene* scene, const XpUiPaint*) -> uint8_t {
    int row = menu - 1;
    if (menu >= 4 && !(settings.plugins || settings.opds)) --row;
    for (int i = 0; i < scene->command_count; ++i) {
      const auto& c = scene->commands[i];
      if (c.resource == (settings.grid ? 0x7000u + menu - 1 : 0x4000u + row)) {
        menuRect = c.rect;
        if (menuRect.w == 0) menuRect.w = 1;
        if (menuRect.h == 0) menuRect.h = 1;
      }
    }
    return 1;
  };
  const XpAppPlatform platform{&legacy, &resources, &runtime};
  assert(xp_app_init(&platform, &compatibility) == XP_APP_OK);
}
void factoryCase(int argc, char** argv) {
  assert(argc == 8);
  settings.grid = std::atoi(argv[2]);
  books = std::atoi(argv[3]);
  settings.plugins = std::atoi(argv[4]);
  int servers = std::atoi(argv[5]);
  OPDS_STORE.servers.resize(servers, 42);
  settings.opds = servers != 0;
  menu = std::atoi(argv[6]);
  bool touch = std::atoi(argv[7]);
  init();
  XpAppEvent e{.kind = XP_APP_HOME_REQUESTED};
  e.value.home = {uint8_t(menu), 0};
  event(e);
  paint();
  paint();
  if (touch) {
    assert(menuRect.w > 0 && menuRect.h > 0);
    input.touch_x = menuRect.x + menuRect.w / 2;
    input.touch_y = menuRect.y + menuRect.h / 2;
    input.touch_flags = XP_UI_TOUCH_PRESSED;
    xp_app_loop();
    input.touch_flags = XP_UI_TOUCH_RELEASED;
  } else
    input.released = XP_UI_CONFIRM;
  xp_app_loop();
  auto* object = xp_app_test_current_legacy();
  assert(object);
  auto* activity = nativeActivity(object);
  if (menu == 1) assert(static_cast<FileBrowserActivity*>(activity)->basepath == "/");
  if (menu == 2) assert(created == XP_APP_LIBRARY);
  if (menu == 3) {
    assert(created == (settings.plugins ? XP_APP_PLUGINS : servers == 1 ? XP_APP_OPDS : XP_APP_OPDS_PICKER));
    if (settings.plugins)
      assert(flag == (servers != 0));
    else if (servers == 1)
      assert(flag == 42);
  }
  if (menu == 4) assert(created == XP_APP_FILE_TRANSFER && flag == 0);
  if (menu == 5) assert(created == XP_APP_SETTINGS && flag == 6);
}
void resultCase() {
  init();
  auto* grand = new Activity;
  adopt(grand, false);
  for (bool cancelled : {false, true}) {
    ActivityResult cases[] = {ActivityResult{},
                              WifiResult{true, "ssid", "ip"},
                              KeyboardResult{"keyboard"},
                              MenuResult{7, 2, 3},
                              ChapterResult{9, "anchor"},
                              PercentResult{42},
                              IntervalResult{1234},
                              PageResult{88},
                              ClippingResult{.action = ClippingResult::Action::Bookmark,
                                             .text = "clipping",
                                             .startPageOffset = 11,
                                             .endPageOffset = 29,
                                             .startWordIndex = 41,
                                             .endWordIndex = 67,
                                             .wordCount = 3,
                                             .startOffset = 0x12345678,
                                             .endOffset = 0x9abcdef0},
                              ProgressChangeResult{.spineIndex = 4,
                                                   .page = 17,
                                                   .totalPages = 91,
                                                   .xpath = "xpath",
                                                   .percentage = 0.375f,
                                                   .hasSavedProgress = true,
                                                   .hasVisibleTextOffset = true,
                                                   .visibleTextOffset = 0xa1b2c3d4},
                              NetworkModeResult{static_cast<NetworkMode>(1)},
                              FootnoteResult{"href"},
                              FilePathResult{"/file"}};
    for (size_t index = 0; index < std::size(cases); ++index) {
      grand->resultHandler = [index, cancelled](const ActivityResult& r) {
        assert(!locked && r.data.index() == index);
        if (cancelled)
          assert(r.isCancelled);
        else
          assert(!r.isCancelled);
        switch (index) {
          case 1:
            assert(std::get<WifiResult>(r.data).ssid == "ssid" && std::get<WifiResult>(r.data).ip == "ip" &&
                   std::get<WifiResult>(r.data).connected);
            break;
          case 2:
            assert(std::get<KeyboardResult>(r.data).text == "keyboard");
            break;
          case 3:
            assert(std::get<MenuResult>(r.data).action == 7 && std::get<MenuResult>(r.data).orientation == 2 &&
                   std::get<MenuResult>(r.data).pageTurnOption == 3);
            break;
          case 4:
            assert(std::get<ChapterResult>(r.data).anchor == "anchor" &&
                   std::get<ChapterResult>(r.data).spineIndex == 9);
            break;
          case 5:
            assert(std::get<PercentResult>(r.data).percent == 42);
            break;
          case 6:
            assert(std::get<IntervalResult>(r.data).value == 1234);
            break;
          case 7:
            assert(std::get<PageResult>(r.data).page == 88);
            break;
          case 8:
            assert(std::get<ClippingResult>(r.data).text == "clipping" &&
                   std::get<ClippingResult>(r.data).wordCount == 3);
            break;
          case 9:
            assert(std::get<ProgressChangeResult>(r.data).xpath == "xpath" &&
                   std::get<ProgressChangeResult>(r.data).hasSavedProgress);
            break;
          case 10:
            assert(int(std::get<NetworkModeResult>(r.data).mode) == 1);
            break;
          case 11:
            assert(std::get<FootnoteResult>(r.data).href == "href");
            break;
          case 12:
            assert(std::get<FilePathResult>(r.data).path == "/file");
            break;
        }
      };
      auto* child = new Activity;
      child->result = std::move(cases[index]);
      child->result.isCancelled = cancelled;
      adopt(child, true);
      xp_app_loop();
      const auto checkedBefore = resultViewChecks[index][cancelled];
      xp_test_fail_allocation_after(0);
      denyNative = true;
      pop();
      xp_app_loop();
      denyNative = false;
      assert(xp_test_allocation_attempts() == 0);
      assert(resultViewChecks[index][cancelled] == checkedBefore + 1);
      xp_test_fail_allocation_after(SIZE_MAX);
    }
  }
  destroyed = 0;

  auto* parent = new Activity;
  adopt(parent, true);
  xp_app_loop();
  auto* child = new Activity;
  child->result = ClippingResult{.text = std::string(4096, 'x')};
  const auto* original = std::get<ClippingResult>(child->result.data).text.data();
  grand->resultHandler = [&](const ActivityResult& r) {
    assert(!locked && r.data.index() == 0);
    ++deliveries;
  };
  parent->resultHandler = [&](const ActivityResult& r) {
    assert(!locked && destroyed == 1 && paints >= 1);
    const auto& c = std::get<ClippingResult>(r.data);
    assert(c.text.size() == 4096 && c.text.data() == original);
    ++deliveries;
    pop();
    assert(destroyed == 1);  // Enqueued, not drained within this slot.
  };
  adopt(child, true);
  xp_app_loop();
  renderOnUnlock = true;
  xp_test_fail_allocation_after(0);
  denyNative = true;
  pop();
  xp_app_loop();
  denyNative = false;
  assert(xp_test_allocation_attempts() == 0);
  xp_test_fail_allocation_after(SIZE_MAX);
  assert(deliveries == 2 && destroyed == 2 && diagnostics == 0 && nativeAttempts == 0);
  // Reentrant replacement is also deferred until the slot returns.
  auto* replacement = new Activity;
  parent = new Activity;
  adopt(parent, true);
  xp_app_loop();
  grand->resultHandler = [&](const ActivityResult&) {
    ++deliveries;
    adopt(replacement, false);
    assert(destroyed == 3);
  };
  pop();
  xp_app_loop();
  assert(deliveries == 3 && destroyed == 4);
  std::puts("maximum clipping: one 4097-byte native text allocation, zero Rust text bytes, zero delivery allocations");
}
int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "factory")
    factoryCase(argc, argv);
  else
    resultCase();
}
