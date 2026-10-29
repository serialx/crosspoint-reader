#include <xp_ui_reader.h>
#include <xp_ui_reader_chrome.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#define taskENTER_CRITICAL(mutex) ((void)(mutex))
#define taskEXIT_CRITICAL(mutex) ((void)(mutex))
#define LOG_ERR(...) ((void)0)
struct MappedInputManager {
  enum class Button { Back, Confirm, NavNext, NavPrevious };
  uint8_t pressed = 0, released = 0;
  bool touchReleased = false;
  bool wasPressed(Button button) const { return pressed & (1u << static_cast<unsigned>(button)); }
  bool wasReleased(Button button) const { return released & (1u << static_cast<unsigned>(button)); }
  unsigned long getHeldTime() const { return 0; }
};
struct Touch {
  bool touchPressed = false, touchReleased = false;
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
struct RenderLock {};
struct GfxRenderer {
  void clearScreen() {}
  void displayBuffer() {}
};
struct ReaderSession {
  void onRenderComplete(uint32_t, int64_t, int) {}
};
static uint32_t millis() { return 0; }
namespace trustedtime {
int64_t trustedNow() { return 0; }
}  // namespace trustedtime
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct EndOfBookOptions {
  enum class Action { None, Redraw, OpenBook, GoHome, LastPage };
  XpUiReaderListState state{};
  XpUiReaderHitLayout published{};
  uint16_t rowCount = 2;
  int mutex = 0;
  bool ready = false;
  bool loaded = false;
  void (*atFirstPaint)(EndOfBookOptions&) = nullptr;
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
struct ReaderActivity {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  struct ModelLock {
    explicit ModelLock(ReaderActivity&) {}
  };
  XpUiReaderBaseModel baseModel{};
  XpUiReaderBaseModel* renderFrame = nullptr;
  uint32_t readBaseField(uint8_t) const;
  void writeBaseField(uint8_t, uint32_t, bool fromRender = false);
  uint8_t runBase(uint8_t, uint8_t, uint8_t);
  bool pageRendered = false;
  std::string bookPath = "/book.epub";
  ReaderSession readerSession;
  bool isAtEndOfBook() const { return true; }
  bool endOfBookMenuActive() const;
  void onEndOfBookRendered() {}
  void markPageRendered() {
    pageRendered = true;
    writeBaseField(XP_UI_READER_BASE_FIELD_RENDERED, 1, true);
  }
  int getProgressBasisPoints() const { return 10000; }
  void renderBook() { assert(false); }
  void render(RenderLock&&);
};
uint8_t ReaderActivity::runBase(uint8_t operation, uint8_t a, uint8_t b) {
  const XpUiReaderBaseServices services{
      this,
      [](void* p, uint8_t field) { return static_cast<ReaderActivity*>(p)->readBaseField(field); },
      [](void* p, uint8_t field, uint32_t value) {
        static_cast<ReaderActivity*>(p)->writeBaseField(field, value, true);
      },
      [](void* p, uint8_t query) -> uint32_t {
        auto& o = *static_cast<ReaderActivity*>(p);
        switch (query) {
          case XP_UI_READER_BASE_QUERY_AT_END:
            return o.isAtEndOfBook();
          case XP_UI_READER_BASE_QUERY_HAS_END:
            return bool(o.endOfBookOptions);
          case XP_UI_READER_BASE_QUERY_END_MENU:
            return o.endOfBookOptions->menuActive();
          default:
            assert(false);
            return 0;
        }
      },
      [](void* p, uint8_t command, int32_t, int32_t) -> uint32_t {
        auto& o = *static_cast<ReaderActivity*>(p);
        switch (command) {
          case XP_UI_READER_BASE_COMMAND_CLEAR_SCREEN:
            o.renderer.clearScreen();
            break;
          case XP_UI_READER_BASE_COMMAND_END_LOAD:
            o.endOfBookOptions->loadOnce(o.bookPath);
            break;
          case XP_UI_READER_BASE_COMMAND_END_RENDER:
            o.endOfBookOptions->render(o.renderer, o.mappedInput);
            break;
          case XP_UI_READER_BASE_COMMAND_DISPLAY:
            o.renderer.displayBuffer();
            break;
          case XP_UI_READER_BASE_COMMAND_END_RENDERED:
            o.onEndOfBookRendered();
            break;
          case XP_UI_READER_BASE_COMMAND_MARK_RENDERED:
            o.markPageRendered();
            break;
          case XP_UI_READER_BASE_COMMAND_SESSION_RENDER:
            break;
          default:
            assert(false);
        }
        return 0;
      },
      [](void*) -> XpUiReaderTurnInput {
        assert(false);
        return {};
      },
      [](void*) -> XpUiReaderTurnInput {
        assert(false);
        return {};
      }};
  return xp_ui_reader_base_run(&services, operation, a, b);
}
// PREPAINT_METHODS
static ReaderActivity* active;
static unsigned tested;
static void beforeFirstPaint(EndOfBookOptions& end) {
  assert(active->readBaseField(XP_UI_READER_BASE_FIELD_END_READY) && active->endOfBookMenuActive());
  assert(!end.ready && !active->pageRendered);
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
  ReaderActivity reader;
  reader.endOfBookOptions = std::make_unique<EndOfBookOptions>(reader.renderer);
  reader.endOfBookOptions->atFirstPaint = beforeFirstPaint;
  active = &reader;
  reader.render(RenderLock{});
  assert(tested == 8 && reader.pageRendered && reader.endOfBookOptions->ready);
}
