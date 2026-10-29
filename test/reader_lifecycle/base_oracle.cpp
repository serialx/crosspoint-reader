#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <tuple>
#include <utility>
static unsigned eventId;
static void event(const char* name, int a = 0, int b = 0) { std::printf("%u %s %d %d\n", eventId++, name, a, b); }
struct Allocation {
  void* pointer;
  size_t bytes;
};
static Allocation allocations[128]{};
static bool recordAllocations = false;
void* operator new(size_t bytes) {
  void* pointer = std::malloc(bytes ? bytes : 1);
  if (!pointer) throw std::bad_alloc();
  if (recordAllocations) {
    bool tracked = false;
    for (auto& slot : allocations) {
      if (!slot.pointer) {
        slot = {pointer, bytes};
        tracked = true;
        break;
      }
    }
    if (!tracked) std::abort();
    event("allocate-bytes", static_cast<int>(bytes));
  }
  return pointer;
}
void operator delete(void* pointer) noexcept {
  for (auto& slot : allocations) {
    if (pointer && slot.pointer == pointer) {
      event("release-bytes", static_cast<int>(slot.bytes));
      slot = {};
      break;
    }
  }
  std::free(pointer);
}
void operator delete(void* pointer, size_t) noexcept { ::operator delete(pointer); }
template <typename... Args>
static void log(const char* tag, const char* fmt, Args... args) {
  std::printf("%u %s ", eventId++, tag);
  if constexpr (sizeof...(args) == 0)
    std::printf("%s", fmt);
  else
    std::printf(fmt, args...);
  std::puts("");
}
#define LOG_INF(...) log(__VA_ARGS__)
#define LOG_ERR(...) log(__VA_ARGS__)
static unsigned long millis() {
  event("now");
  return 12000;
}
namespace trustedtime {
static int64_t trustedNow() {
  event("epoch");
  return 123456;
}
}  // namespace trustedtime
struct RenderLock {
  template <typename T>
  explicit RenderLock(T&) {
    event("lock");
  }
  RenderLock() { event("lock"); }
  ~RenderLock() { event("unlock"); }
};
struct FontCache {
  void releaseSdFontCaches() { event("font-release"); }
};
struct GfxRenderer {
  enum class Orientation { Portrait };
  bool font = true;
  FontCache cache;
  FontCache* getFontCacheManager() {
    event("font-cache", font);
    return font ? &cache : nullptr;
  }
  void setOrientation(Orientation o) { event("orientation", int(o)); }
  void clearScreen() { event("clear"); }
  void displayBuffer() { event("display"); }
};
struct MappedInputManager {
  unsigned long held = 0;
  unsigned long getHeldTime() {
    event("held", held);
    return held;
  }
};
struct Activity {
  void onEnter() { event("base-enter"); }
  void onExit() { event("base-exit"); }
};
struct {
  bool existsValue = true;
  bool exists(const char* p) {
    event("exists", existsValue);
    std::puts(p);
    return existsValue;
  }
} Storage;
struct {
  unsigned getFreeHeap() {
    event("heap-free");
    return 50000;
  }
  unsigned getMaxAllocHeap() {
    event("heap-max");
    return 30000;
  }
} ESP;
struct {
  std::string openEpubPath;
  int readerActivityLoadCount = 0;
  void saveToFile() {
    event("state-save", readerActivityLoadCount);
    std::puts(openEpubPath.c_str());
  }
} APP_STATE;
struct {
  void ensureLoaded(GfxRenderer&) { event("fonts-load"); }
} sdFontSystem;
struct {
  void addBook(const std::string& p, const std::string& t, const std::string& a, const std::string& b) {
    event("recent-add");
    std::printf("%s|%s|%s|%s\n", p.c_str(), t.c_str(), a.c_str(), b.c_str());
  }
} RECENT_BOOKS;
namespace pluginevents {
enum class Event { ReaderOpen, ReaderExit, ReaderSession };
struct Var {
  const char* name;
  const char* value;
};
static bool subscribed = true;
static bool anySubscriber(Event e) {
  event("subscriber", int(e), subscribed);
  return subscribed;
}
static void emit(Event e, const Var* v, int n) {
  event("emit", int(e), n);
  for (int i = 0; i < n; ++i) std::printf("%s=%s\n", v[i].name, v[i].value);
}
}  // namespace pluginevents
namespace KOReaderDocumentId {
static std::string value(32, 'a');
static std::string calculate(const std::string& p) {
  event("document");
  std::puts(p.c_str());
  return value;
}
}  // namespace KOReaderDocumentId
struct ReaderSession {
  bool worthy = true;
  void reset() {
    event("session-reset");
    worthy = false;
  }
  bool isEmitWorthy() {
    event("session-worthy", worthy);
    return worthy;
  }
  int64_t startTime() {
    event("start-time");
    return 1234;
  }
  int64_t endTime() {
    event("end-time");
    return 1256;
  }
  unsigned durationSeconds() {
    event("duration");
    return 22;
  }
  unsigned startProgressBp() {
    event("start-progress");
    return 111;
  }
  unsigned endProgressBp() {
    event("end-progress");
    return 222;
  }
  void onRenderComplete(uint32_t ms, int64_t epoch, int bp) {
    event("render-complete", ms, bp);
    std::printf("epoch %lld\n", static_cast<long long>(epoch));
  }
};
static bool allocate = true;
template <typename T, typename... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  event("allocate", allocate);
  if (!allocate) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct EndOfBookOptions {
#include "base_action.inc"
  bool menu = true;
  Action action = Action::None;
  explicit EndOfBookOptions(GfxRenderer&) { event("end-create"); }
  ~EndOfBookOptions() { event("end-drop"); }
  bool menuActive() {
    event("end-active", menu);
    return menu;
  }
  void loadOnce(const std::string& p) {
    event("end-load");
    std::puts(p.c_str());
  }
  void render(GfxRenderer&, MappedInputManager&) { event("end-render"); }
  Action handleMenuInput(MappedInputManager&, std::string* p) {
    event("end-input", int(action));
    *p = "/library/next-very-long-path.epub";
    return action;
  }
};
struct {
  void goToReader(const std::string& p) {
    event("navigate");
    std::puts(p.c_str());
  }
} activityManager;
struct {
  enum { CHAPTER_SKIP = 1 };
  int longPressButtonBehavior = 0;
} SETTINGS;
namespace ReaderUtils {
constexpr unsigned long SKIP_HOLD_MS = 700;
struct Touch {
  bool prev = false, next = false;
  unsigned long heldMs = 0;
};
static Touch touch;
static bool prev = false, next = false, tilt = false;
static Touch detectTouchPageTurn(GfxRenderer&, MappedInputManager&) {
  event("touch", touch.prev, touch.next);
  return touch;
}
static auto detectPageTurn(MappedInputManager&) {
  event("buttons", prev, next);
  return std::tuple{prev, next, tilt};
}
}  // namespace ReaderUtils
namespace haptic_feedback {
static void touchAction(bool skip) { event("haptic", skip); }
}  // namespace haptic_feedback
struct ReaderActivity : Activity {
  GfxRenderer renderer;
  MappedInputManager mappedInput;
  std::string bookPath = "/book.epub";
  int pagesUntilFullRefresh = 3;
  bool forcedRefreshPending = false;
  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};
  ReaderSession readerSession;
  std::atomic<bool> pageRendered{false};
  bool bookRemembered = false;
  bool atEnd = false, format = false, back = false, changed = true, load = true, failureHandled = false;
  struct Info {
    int progressPercent;
  };
  Info getScreenshotInfo() {
    event("screenshot-info");
    return {12};
  }
  int getProgressBasisPoints() {
    event("basis-points");
    return 1234;
  }
  void finish() { event("finish"); }
  void requestUpdate() { event("update"); }
  bool loadBook() {
    event("load", load);
    return load;
  }
  bool handleLoadFailure() {
    event("load-failure", failureHandled);
    return failureHandled;
  }
  void applyInitialOrientation() { event("initial-orientation"); }
  std::string getBookTitle() {
    event("title");
    return "Title";
  }
  std::string getBookAuthor() {
    event("author");
    return "Author";
  }
  std::string getBookThumbBmpPath() {
    event("thumb");
    return "Thumb";
  }
  bool isAtEndOfBook() const {
    event("at-end", atEnd);
    return atEnd;
  }
  bool handleFormatInput() {
    event("format", format);
    return format;
  }
  bool handleBackNavigation() {
    event("back", back);
    return back;
  }
  bool pageTurn(bool forward) {
    event("turn", forward, changed);
    return changed;
  }
  bool skipPages(int n) {
    event("skip", n, changed);
    return changed;
  }
  void notePageTurn(bool forward, bool succeeded) { event("note-turn", forward, succeeded); }
  void onGoHome() { event("home"); }
  void onReturnFromEndOfBook() { event("return"); }
  void renderBook() { event("render-book"); }
  void onEndOfBookRendered() { event("end-rendered"); }
  void markPageRendered() {
    event("mark-rendered");
    pageRendered.store(true);
  }
  void rememberBookOnceRendered();
  void clearEndOfBookOptionsIfNeeded();
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu(bool = false);
  bool handleEndOfBookPageTurn(bool, bool);
  void loop();
  void render(RenderLock&&);
  bool handleForcedRefresh();
  void onEnter();
  void onExit();
  void flushReaderSession();
};
#include "base_methods.inc"
int main() {
  recordAllocations = true;
  for (unsigned phase = 0; phase < 24; ++phase)
    for (unsigned action = 0; action < 24; ++action) {
      eventId = 0;
      std::printf("CASE %u %u\n", phase, action);
      ReaderActivity a;
      APP_STATE.openEpubPath = phase & 1 ? "/old.epub" : "";
      APP_STATE.readerActivityLoadCount = 7;
      Storage.existsValue = phase != 1;
      allocate = phase != 6;
      a.load = phase != 3 && phase != 4;
      a.failureHandled = phase == 4;
      a.renderer.font = phase != 5;
      a.atEnd = phase >= 6 && phase < 18;
      if (phase >= 7 && phase < 20) {
        a.endOfBookOptions = std::make_unique<EndOfBookOptions>(a.renderer);
        a.endOfBookOptions->menu = phase != 8 && phase != 19;
        a.endOfBookOptions->action = static_cast<EndOfBookOptions::Action>(action % 5);
      }
      a.endOfBookOptionsReady = phase >= 9 && phase < 20;
      a.pageRendered = phase & 1;
      a.bookRemembered = phase == 20;
      a.readerSession.worthy = phase != 21;
      pluginevents::subscribed = phase != 22;
      KOReaderDocumentId::value = phase == 23 ? std::string(32, 'z') : std::string(32, 'a');
      a.format = action == 1;
      a.back = action == 2;
      a.changed = action != 3;
      SETTINGS.longPressButtonBehavior = (action / 4) % 2;
      ReaderUtils::prev = (action & 1) != 0;
      ReaderUtils::next = (action & 2) != 0;
      ReaderUtils::tilt = (action & 8) != 0;
      ReaderUtils::touch = {action >= 16 && (action & 1), action >= 16 && (action & 2),
                            action % 3 == 0 ? 699UL : 700UL};
      a.mappedInput.held = action % 3 == 0 ? 699 : 700;
      a.onEnter();
      a.loop();
      a.render(RenderLock{});
      a.loop();
      a.handleEndOfBookMenu(true);
      a.handleEndOfBookPageTurn(action & 1, action & 2);
      a.handleForcedRefresh();
      if (action & 1) a.flushReaderSession();
      a.onExit();
      event("state", a.pagesUntilFullRefresh, a.forcedRefreshPending);
      event("publication", a.pageRendered.load(), a.endOfBookOptionsReady.load());
      event("remembered", a.bookRemembered);
    }
}
