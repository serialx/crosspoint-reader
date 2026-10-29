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
  std::atomic<bool> endOfBookOptionsReady{false};
  bool pageRendered = false;
  std::string bookPath = "/book.epub";
  ReaderSession readerSession;
  bool isAtEndOfBook() const { return true; }
  bool endOfBookMenuActive() const;
  void onEndOfBookRendered() {}
  void markPageRendered() { pageRendered = true; }
  int getProgressBasisPoints() const { return 10000; }
  void renderBook() { assert(false); }
  void render(RenderLock&&);
};
// PREPAINT_METHODS
static ReaderActivity* active;
static unsigned tested;
static void beforeFirstPaint(EndOfBookOptions& end) {
  assert(active->endOfBookOptionsReady.load() && active->endOfBookMenuActive());
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
