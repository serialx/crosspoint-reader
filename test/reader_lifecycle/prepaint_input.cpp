#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base_owner_fixture.h"
#include "overlay_actions_session_fixture.h"

#define taskENTER_CRITICAL(mutex) ((void)(mutex))
#define taskEXIT_CRITICAL(mutex) ((void)(mutex))
#define LOG_ERR(...) ((void)0)
struct MappedInputManager {
  enum class Button { Back, Confirm, NavNext, NavPrevious };
  uint16_t pressed = 0, released = 0, held = 0;
  bool touchReleased = false;
  bool wasPressed(Button button) const { return pressed & (1u << static_cast<unsigned>(button)); }
  bool wasReleased(Button button) const { return released & (1u << static_cast<unsigned>(button)); }
  bool isPressed(Button button) const { return held & (1u << static_cast<unsigned>(button)); }
  unsigned long getHeldTime() const { return 0; }
};
struct Touch {
  bool touchPressed = false, touchReleased = false, touchHeld = false;
  int16_t touchX = 10, touchY = 10;
};
static Touch touchSnapshotFrom(const MappedInputManager& input) { return {false, input.touchReleased}; }
namespace haptic_feedback {
void touchAction(bool) { assert(false); }
}  // namespace haptic_feedback
struct CrossPointSettings {
  enum { OFF = 0 };
};
struct {
  int longPressButtonBehavior = 0;
} SETTINGS;
static unsigned renderDepth, modelDepth;
struct RenderLock {
  RenderLock() {
    assert(renderDepth == 0 && modelDepth == 0);
    ++renderDepth;
  }
  ~RenderLock() {
    assert(renderDepth == 1 && modelDepth == 0);
    --renderDepth;
  }
};
struct GfxRenderer {
  void clearScreen() {}
  void displayBuffer() {}
};
struct EndOfBookOptions {
  enum class Action { None, Redraw, OpenBook, GoHome, LastPage };
  XpUiReaderListState state{};
  XpUiReaderHitLayout published{};
  uint16_t rowCount = 2;
  int mutex = 0;
  bool ready = false;
  bool sceneFailed = false, suppressedContact = false, lastContact = false;
  uint32_t suppressed = 0, lastHeld = 0;
  bool loaded = false;
  void (*atFirstPaint)(EndOfBookOptions&) = &OverlayUnexpected<void (*)(EndOfBookOptions&)>::call;
  explicit EndOfBookOptions(GfxRenderer&) { xp_ui_reader_list_init(&state, 0); }
  void loadOnce(const std::string&) { loaded = true; }
  bool menuActive() const { return loaded; }
  std::string fullPath(size_t index) const { return index == 0 ? "/next.epub" : ""; }
  Action handleMenuInput(const MappedInputManager&, std::string*);
  void render(GfxRenderer&, const MappedInputManager&) {
    assert(loaded && !ready);
    if (atFirstPaint) atFirstPaint(*this);
    ready = true;
  }
};
struct ReaderFixture {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  void* owner = nullptr;
  unsigned markCalls = 0;
  XpEpubTestBaseObservation observe() const {
    XpEpubTestBaseObservation value{};
    xp_epub_test_base_observe(owner, &value);
    return value;
  }
  bool endOfBookMenuActive() const { return xp_epub_test_base_end_active(owner) != 0; }
  void initialize();
  ~ReaderFixture();
};
static ReaderFixture* active;
static void leaf() { assert(active && modelDepth == 0 && renderDepth == 1); }
static void context(void* value) {
  leaf();
  assert(value == active);
}
static const XpAppResources resources = overlayUnexpectedResources();
static const XpAppRuntime runtime = [] {
  auto table = overlayUnexpectedRuntime();
  table.model_lock = [] {
    assert(active && modelDepth == 0);
    ++modelDepth;
  };
  table.model_unlock = [] {
    assert(active && modelDepth == 1);
    --modelDepth;
  };
  return table;
}();
static XpUiReaderRenderServices renderServices = {
    nullptr,
    &OverlayUnexpected<uint32_t (*)(void*, uint8_t)>::call,
    [](void* value, const XpUiReaderRenderOp* op) -> uint32_t {
      context(value);
      assert(op);
      switch (op->kind) {
        case XP_UI_READER_RENDER_CLEAR:
          assert(op->value == 255);
          active->renderer.clearScreen();
          break;
        case XP_UI_READER_RENDER_DISPLAY:
          assert(op->mode == XP_UI_FAST);
          active->renderer.displayBuffer();
          break;
        default:
          assert(false && "unexpected prepaint render operation");
          std::abort();
      }
      return 0;
    },
};
static const XpEpubLeaves leaves = [] {
  auto table = overlayUnexpectedLeaves();
  table.render_services = [](void* value) -> const XpUiReaderRenderServices* {
    context(value);
    renderServices.context = value;
    return &renderServices;
  };
  table.end_load = [](void* value, XpAppText path) {
    leaf();
    assert(value == active->endOfBookOptions.get() && path.data);
    const std::string text(reinterpret_cast<const char*>(path.data), path.len);
    assert(text == "/book.epub");
    active->endOfBookOptions->loadOnce(text);
  };
  table.end_paint = [](void* value) {
    leaf();
    assert(value == active->endOfBookOptions.get());
    active->endOfBookOptions->render(active->renderer, active->mappedInput);
  };
  table.end_menu_active = [](void* value) -> uint8_t {
    leaf();
    assert(value == active->endOfBookOptions.get());
    return active->endOfBookOptions->menuActive();
  };
  table.end_close = [](void* value) {
    leaf();
    assert(value == active->endOfBookOptions.get());
    active->endOfBookOptions.reset();
  };
  return table;
}();
void ReaderFixture::initialize() {
  assert(active == this && !owner && endOfBookOptions && renderDepth == 0 && modelDepth == 0);
  XpEpubTestBaseSeed seed{};
  seed.native = this;
  seed.end = endOfBookOptions.get();
  static const uint8_t path[] = "/book.epub";
  seed.path = {path, sizeof(path) - 1};
  seed.hooks.context = this;
  seed.hooks.enter = &OverlayUnexpected<decltype(seed.hooks.enter)>::call;
  seed.hooks.exit = &OverlayUnexpected<decltype(seed.hooks.exit)>::call;
  seed.hooks.orientation = &OverlayUnexpected<decltype(seed.hooks.orientation)>::call;
  seed.hooks.load = &OverlayUnexpected<decltype(seed.hooks.load)>::call;
  seed.hooks.failure = &OverlayUnexpected<decltype(seed.hooks.failure)>::call;
  seed.hooks.at_end = &OverlayUnexpected<decltype(seed.hooks.at_end)>::call;
  seed.hooks.return_end = &OverlayUnexpected<decltype(seed.hooks.return_end)>::call;
  seed.hooks.end_rendered = &OverlayUnexpected<decltype(seed.hooks.end_rendered)>::call;
  seed.hooks.back = &OverlayUnexpected<decltype(seed.hooks.back)>::call;
  seed.hooks.format = &OverlayUnexpected<decltype(seed.hooks.format)>::call;
  seed.hooks.touch = &OverlayUnexpected<decltype(seed.hooks.touch)>::call;
  seed.hooks.buttons = &OverlayUnexpected<decltype(seed.hooks.buttons)>::call;
  seed.hooks.turn = &OverlayUnexpected<decltype(seed.hooks.turn)>::call;
  seed.hooks.skip = &OverlayUnexpected<decltype(seed.hooks.skip)>::call;
  seed.hooks.snapshot = &OverlayUnexpected<decltype(seed.hooks.snapshot)>::call;
  seed.hooks.worthy = &OverlayUnexpected<decltype(seed.hooks.worthy)>::call;
  seed.hooks.reset = &OverlayUnexpected<decltype(seed.hooks.reset)>::call;
  seed.hooks.complete = &OverlayUnexpected<decltype(seed.hooks.complete)>::call;
  seed.hooks.mark = &OverlayUnexpected<decltype(seed.hooks.mark)>::call;
  seed.hooks.note = &OverlayUnexpected<decltype(seed.hooks.note)>::call;
  seed.hooks.force = &OverlayUnexpected<decltype(seed.hooks.force)>::call;
  seed.hooks.percent = &OverlayUnexpected<decltype(seed.hooks.percent)>::call;
  seed.hooks.home = &OverlayUnexpected<decltype(seed.hooks.home)>::call;
  seed.hooks.navigate = &OverlayUnexpected<decltype(seed.hooks.navigate)>::call;
  seed.finish = &OverlayUnexpected<decltype(seed.finish)>::call;
  seed.update = &OverlayUnexpected<decltype(seed.update)>::call;
  seed.hooks.at_end = [](void* value) -> uint8_t {
    context(value);
    return 1;
  };
  seed.hooks.end_rendered = [](void* value) { context(value); };
  seed.hooks.complete = [](void* value) { context(value); };
  seed.hooks.mark = [](void* value) {
    context(value);
    assert(!active->observe().rendered);
    ++active->markCalls;
  };
  owner = xp_epub_test_base_create(&seed, &leaves, &runtime, &resources);
  assert(owner);
}
ReaderFixture::~ReaderFixture() {
  assert(owner && active == this);
  RenderLock lock;
  xp_epub_test_base_destroy(owner);
  owner = nullptr;
  assert(!endOfBookOptions);
  active = nullptr;
}
// PREPAINT_METHODS
static unsigned tested;
static void beforeFirstPaint(EndOfBookOptions& end) {
  const auto observed = active->observe();
  assert(observed.end_ready && active->endOfBookMenuActive());
  assert(!end.ready && !observed.rendered && active->markCalls == 0);
  struct Case {
    uint8_t button;
    EndOfBookOptions::Action action;
  };
  const Case cases[] = {{1, EndOfBookOptions::Action::LastPage},
                        {2, EndOfBookOptions::Action::OpenBook},
                        {4, EndOfBookOptions::Action::Redraw},
                        {8, EndOfBookOptions::Action::Redraw}};
  for (const bool usePress : {false, true}) {
    SETTINGS.longPressButtonBehavior = usePress ? CrossPointSettings::OFF : 1;
    for (const auto& c : cases) {
      xp_ui_reader_list_init(&end.state, 0);
      MappedInputManager input;
      if (usePress && c.button >= 4)
        input.pressed = c.button;
      else
        input.released = c.button;
      std::string path;
      assert(end.handleMenuInput(input, &path) == c.action);
      if (c.action == EndOfBookOptions::Action::OpenBook) assert(path == "/next.epub");
      ++tested;
    }
  }
  xp_ui_reader_list_init(&end.state, 0);
  MappedInputManager touch;
  touch.touchReleased = true;
  std::string path;
  assert(end.handleMenuInput(touch, &path) == EndOfBookOptions::Action::None);
  // ActivityManager's existing gate sees the held render lock, with either
  // first-open or previously rendered reader state, and refuses remote turns.
  assert(!xp_ui_reader_remote_allowed(0, 1, 1));
  assert(!xp_ui_reader_remote_allowed(1, 1, 1));
}
int main() {
  ReaderFixture reader;
  reader.endOfBookOptions = std::make_unique<EndOfBookOptions>(reader.renderer);
  reader.endOfBookOptions->atFirstPaint = beforeFirstPaint;
  active = &reader;
  reader.initialize();
  {
    RenderLock lock;
    assert(xp_epub_test_base_render(reader.owner, &OverlayUnexpected<void (*)(void*)>::call, &reader) == 1);
  }
  const auto observed = reader.observe();
  assert(tested == 8 && observed.rendered && observed.end_ready && reader.endOfBookOptions->ready);
  assert(reader.markCalls == 1 && renderDepth == 0 && modelDepth == 0);
}
