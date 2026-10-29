#include <xp_ui_reader.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>
#ifdef BUILD_RELEASE_LOG
#define LOG_DBG(tag, fmt, ...) ((void)0)
#else
#define LOG_DBG(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
#endif
#define LOG_ERR(tag, fmt, ...) std::printf(tag " " fmt "\n", ##__VA_ARGS__)
static void event(const char* name, int a = 0, int b = 0) { std::printf("%s %d %d\n", name, a, b); }
static uint32_t clockMs = 0;
uint32_t millis() {
  event("clock", clockMs);
  return clockMs;
}
void delay(uint32_t ms) {
  event("sleep", ms);
  clockMs += ms;
}
static int lockDepth = 0;
struct RenderLock {
  enum class Mode { Try };
  static inline bool available = true;
  RenderLock() {
    ++lockDepth;
    event("lock", lockDepth);
  }
  explicit RenderLock(Mode) {
    ++lockDepth;
    event("try-lock", available);
  }
  ~RenderLock() {
    event("unlock", lockDepth);
    --lockDepth;
  }
  bool ownsLock() const { return available; }
};
namespace bleturner {
struct Status {
  bool running = false, stopping = false, starting = false;
};
enum class BuildRelease { NotHeld, Released, Pending };
static std::vector<Status> sequence;
static size_t index = 0;
static BuildRelease release = BuildRelease::NotHeld;
static bool heap = false;
Status status() {
  const auto value = sequence[index < sequence.size() ? index++ : sequence.size() - 1];
  event("status", value.running | (value.stopping << 1) | (value.starting << 2));
  return value;
}
BuildRelease beforeChapterBuild() {
  event("release-radio", static_cast<int>(release));
  return release;
}
bool holdsHeap() {
  event("radio-heap", heap);
  return heap;
}
}  // namespace bleturner
// BUILD_GATE
struct {
  size_t free = 32768, block = 16384;
  size_t getFreeHeap() const {
    event("free", free);
    return free;
  }
  size_t getMaxAllocHeap() const {
    event("block", block);
    return block;
  }
} ESP;
struct StripBuffers {
  bool release = false;
  size_t free = 32768, block = 16384;
  bool releaseIdle() {
    event("release-strips", release);
    if (release) {
      ESP.free = free;
      ESP.block = block;
    }
    return release;
  }
};
struct GfxRenderer {
  bool framebuffer = true;
  uint32_t loans = 0;
  bool hasFrameBuffer() const {
    event("framebuffer", framebuffer);
    return framebuffer;
  }
  uint32_t frameBufferLoanCount() const {
    event("loans", loans);
    return loans;
  }
};
constexpr int STR_INDEXING = 1;
int tr(int value) { return value; }
struct {
  void drawPopup(GfxRenderer&, int id) { event("popup", id); }
} GUI;
struct MappedInputManager {
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power };
  unsigned mask = 0;
  bool isPressed(Button button) const {
    const int b = static_cast<int>(button);
    event("pressed", b);
    return mask & (1u << b);
  }
};
static bool rawContact = false;
bool buttonContactClosed() {
  event("raw", rawContact);
  return rawContact;
}
struct Section {
  bool building = true, complete = false, ok = true, loan = false;
  int currentPage = 7;
  GfxRenderer* renderer = nullptr;
  ~Section() { event("section-drop"); }
  bool isBuilding() const {
    event("building", building);
    return building;
  }
  bool isBuildComplete() const {
    event("complete", complete);
    return complete;
  }
  bool buildSomeMore(int count, bool (*stop)()) {
    assert(lockDepth == 1);
    event("build", count, stop());
    if (loan) ++renderer->loans;
    return ok;
  }
};
struct EpubReaderActivity {
  enum class Overlay { None, Toolbar };
  static constexpr uint32_t BUILD_ROOM_WAIT_MS = 5000, BUILD_ROOM_STEP_MS = 50;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32768, BACKGROUND_BUILD_MIN_MAX_ALLOC = 16384;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  GfxRenderer renderer;
  StripBuffers stripBuffers;
  MappedInputManager mappedInput;
  std::unique_ptr<Section> section;
  std::atomic<bool> radioReleasedForBuild{false}, pageBufferStale{false};
  bool buildHeapPaused = false, buildPopupPending = false, reposition = false;
  int refresh = 8, currentSpineIndex = 2, backgroundBuildFailedSpine = -1, nextPageNumber = 0, pendingManualTurn = 0;
  Overlay overlay = Overlay::None;
  int& renderRefreshCounter() { return refresh; }
  void requestUpdate() { event("update"); }
  bool applyDeferredReposition() {
    event("reposition", reposition);
    return reposition;
  }
  bool makeRoomForChapterBuild();
  bool buildTickHeapGate();
  void showBuildPopup();
  void showBuildPopup(GfxRenderer&, int&);
  bool anyButtonPressed() const;
  void backgroundBuildTick();
  bool backgroundBuildWanted() const;
  bool skipLoopDelay();
  bool runBuildPolicy(uint8_t operation) const;
};
// BUILD_METHODS
int main() {
  for (int pattern = 0; pattern < 8; ++pattern) {
    for (int release = 0; release < 3; ++release) {
      for (int released = 0; released < 2; ++released) {
        for (uint32_t start : {0u, UINT32_MAX - 125u}) {
          event("admission", pattern * 6 + release * 2 + released, start);
          bleturner::sequence = {
              {true, false, false}, {false, false, true}, {true, false, false}, {false, false, false}};
          if (pattern < 7) bleturner::sequence = {{bool(pattern & 1), bool(pattern & 2), bool(pattern & 4)}};
          bleturner::index = 0;
          bleturner::release = static_cast<bleturner::BuildRelease>(release);
          clockMs = start;
          EpubReaderActivity a;
          a.radioReleasedForBuild = released;
          event("admission-result", a.makeRoomForChapterBuild(), a.radioReleasedForBuild);
          event("admission-clock", clockMs);
        }
      }
    }
  }
  for (int mask = 0; mask < 128; ++mask) {
    event("buttons", mask);
    EpubReaderActivity a;
    a.mappedInput.mask = mask;
    event("pressed-result", a.anyButtonPressed());
  }
  for (int mask = 0; mask < 4; ++mask) {
    event("popup-case", mask);
    EpubReaderActivity a;
    a.buildPopupPending = mask & 1;
    a.renderer.framebuffer = mask & 2;
    a.showBuildPopup();
    event("popup-state", a.buildPopupPending, a.refresh);
  }
  for (int mask = 0; mask < 16; ++mask) {
    for (int initial = 0; initial < 4; ++initial) {
      event("heap-case", mask, initial);
      EpubReaderActivity a;
      ESP.free = initial & 1 ? 32768 : 32767;
      ESP.block = initial & 2 ? 16384 : 16383;
      a.stripBuffers.release = mask & 1;
      a.stripBuffers.free = mask & 2 ? 32768 : 32767;
      a.stripBuffers.block = mask & 4 ? 16384 : 16383;
      a.buildHeapPaused = mask & 8;
      const bool result = a.buildTickHeapGate();
      event("heap-result", result, a.buildHeapPaused);
    }
  }
  for (int mask = 0; mask < 4096; ++mask) {
    event("background", mask);
    EpubReaderActivity a;
    lockDepth = 0;
    RenderLock::available = !(mask & 1);
    a.overlay = mask & 2 ? EpubReaderActivity::Overlay::Toolbar : EpubReaderActivity::Overlay::None;
    if (!(mask & 4)) {
      a.section = std::make_unique<Section>();
      a.section->renderer = &a.renderer;
    }
    bleturner::heap = mask & 8;
    a.backgroundBuildFailedSpine = mask & 16 ? 2 : -1;
    rawContact = mask & 32;
    a.mappedInput.mask = mask & 64 ? 1u : 0u;
    a.pendingManualTurn = mask & 128 ? -1 : 0;
    ESP.free = mask & 256 ? 32767 : 32768;
    ESP.block = 16384;
    if (a.section) {
      a.section->ok = !(mask & 512);
      a.section->loan = mask & 1024;
      a.section->complete = mask & 2048;
    }
    a.reposition = true;
    a.backgroundBuildTick();
    assert(lockDepth == 0);
    event("background-state", a.nextPageNumber, a.backgroundBuildFailedSpine);
    event("background-flags", a.pageBufferStale, a.buildHeapPaused);
    event("delay", a.skipLoopDelay());
  }
}
