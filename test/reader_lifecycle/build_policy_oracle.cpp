#include <I18nKeys.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "session_fixture.h"

static unsigned modelDepth;
static bool publishing;
static unsigned publicationStep, publicationCount;
static uint16_t publicationValues[5];
struct Section;
static Section* liveSection;
static void leaf() { assert(!modelDepth); }
static void event(const char* name, int a = 0, int b = 0) {
  leaf();
  std::printf("%s %d %d\n", name, a, b);
}
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
    leaf();
    assert(!lockDepth);
    ++lockDepth;
    event("lock", lockDepth);
  }
  explicit RenderLock(Mode) {
    leaf();
    assert(!lockDepth);
    ++lockDepth;
    event("try-lock", available);
  }
  ~RenderLock() {
    event("unlock", lockDepth);
    assert(lockDepth == 1);
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
static uint32_t releaseCost = 0;
static bool heap = false;
Status status() {
  const auto value = sequence[index < sequence.size() ? index++ : sequence.size() - 1];
  event("status", value.running | (value.stopping << 1) | (value.starting << 2));
  return value;
}
BuildRelease beforeChapterBuild() {
  event("release-radio", static_cast<int>(release));
  clockMs += releaseCost;
  return release;
}
bool holdsHeap() {
  event("radio-heap", heap);
  return heap;
}
}  // namespace bleturner
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
  GfxRenderer* renderer = nullptr;
  Section() {
    assert(!liveSection);
    liveSection = this;
  }
  ~Section() {
    leaf();
    assert(liveSection == this && !publishing);
    liveSection = nullptr;
    event("section-drop");
  }
  bool isBuilding() const {
    if (!publishing) event("building", building);
    return building;
  }
  bool isBuildComplete() const {
    if (!publishing) event("complete", complete);
    return complete;
  }
  bool buildSomeMore(int count, bool (*stop)()) {
    assert(lockDepth == 1);
    event("build", count, stop());
    if (loan) ++renderer->loans;
    return ok;
  }
};
// Scalar members are construction inputs; observations always read the Rust owner.
struct BuildFixture {
  enum class Overlay { None, Toolbar };
  GfxRenderer renderer;
  StripBuffers stripBuffers;
  MappedInputManager mappedInput;
  std::unique_ptr<Section> section;
  bool radioReleasedForBuild = false, pageBufferStale = false;
  bool buildHeapPaused = false, buildPopupPending = false, reposition = false;
  int refresh = 8, currentSpineIndex = 2, activePage = 7;
  int backgroundBuildFailedSpine = -1, nextPageNumber = 0, pendingManualTurn = 0;
  Overlay overlay = Overlay::None;
  void* owner = nullptr;
  ~BuildFixture();
  void initialize();
  XpEpubTestBuildObservation observe();
  bool makeRoomForChapterBuild();
  bool buildTickHeapGate();
  void showBuildPopup();
  bool anyButtonPressed();
  void backgroundBuildTick();
  bool backgroundBuildWanted();
  bool skipLoopDelay();
};
static BuildFixture* activeFixture;
#include "build_session_rust.inc"
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
          BuildFixture a;
          a.radioReleasedForBuild = released;
          const bool admitted = a.makeRoomForChapterBuild();
          event("admission-result", admitted, a.observe().radio_released);
          event("admission-clock", clockMs);
        }
      }
    }
  }
  for (uint32_t start : {0u, UINT32_MAX - 125u}) {
    event("slow-radio", start);
    bleturner::sequence = {{true, false, false}};
    bleturner::index = 0;
    bleturner::release = bleturner::BuildRelease::NotHeld;
    bleturner::releaseCost = 3000;
    clockMs = start;
    BuildFixture a;
    assert(!a.makeRoomForChapterBuild());
    assert(clockMs == start + 9100u);
    event("slow-radio-clock", clockMs);
    bleturner::releaseCost = 0;
  }
  for (int mask = 0; mask < 128; ++mask) {
    event("buttons", mask);
    BuildFixture a;
    a.mappedInput.mask = mask;
    event("pressed-result", a.anyButtonPressed());
  }
  for (int mask = 0; mask < 4; ++mask) {
    event("popup-case", mask);
    BuildFixture a;
    a.buildPopupPending = mask & 1;
    a.renderer.framebuffer = mask & 2;
    a.showBuildPopup();
    const auto state = a.observe();
    event("popup-state", state.popup, state.refresh);
  }
  for (int mask = 0; mask < 16; ++mask) {
    for (int initial = 0; initial < 4; ++initial) {
      event("heap-case", mask, initial);
      BuildFixture a;
      ESP.free = initial & 1 ? 32768 : 32767;
      ESP.block = initial & 2 ? 16384 : 16383;
      a.stripBuffers.release = mask & 1;
      a.stripBuffers.free = mask & 2 ? 32768 : 32767;
      a.stripBuffers.block = mask & 4 ? 16384 : 16383;
      a.buildHeapPaused = mask & 8;
      const bool result = a.buildTickHeapGate();
      event("heap-result", result, a.observe().heap_paused);
    }
  }
  for (int mask = 0; mask < 4096; ++mask) {
    event("background", mask);
    BuildFixture a;
    lockDepth = 0;
    RenderLock::available = !(mask & 1);
    a.overlay = mask & 2 ? BuildFixture::Overlay::Toolbar : BuildFixture::Overlay::None;
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
    const auto state = a.observe();
    event("background-state", state.next_page, state.failed_spine);
    event("background-flags", state.stale, state.heap_paused);
    event("delay", a.skipLoopDelay());
  }
}
