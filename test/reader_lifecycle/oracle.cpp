#include <algorithm>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>

#include "ChapterBuildGate.h"
#include "ChapterPosition.h"
#include "EpubReaderUtils.h"
#include "LinkNoteTitle.h"
#include "ReaderSession.h"
#include "RemoteTurnGate.h"

static uint32_t clockMs = 1234;
static bool traceClock = false;
static int lockDepth = 0;
unsigned long millis() {
  if (traceClock) std::printf("clock-lock %d\n", lockDepth);
  return clockMs;
}
static bool radioHeap = false;
namespace bleturner {
void acknowledgeLinkNote() { std::puts("ack-link"); }
bool holdsHeap() { return radioHeap; }
}  // namespace bleturner

struct RenderLock {
  enum class Mode { Try };
  static inline bool available = true;
  RenderLock() { ++lockDepth; }
  explicit RenderLock(Mode) { ++lockDepth; }
  ~RenderLock() { --lockDepth; }
  bool ownsLock() const { return available; }
  static bool peek() { return !available; }
};
struct HeapStub {
  size_t free = 32768;
  size_t block = 16384;
  size_t getFreeHeap() const { return free; }
  size_t getMaxAllocHeap() const { return block; }
} ESP;
namespace HalDisplay {
enum Mode { HALF_REFRESH, FAST_REFRESH };
enum class GrayscaleBase { Separate, Combined };
}  // namespace HalDisplay
struct GfxRenderer {
  struct Caps {
    HalDisplay::GrayscaleBase base;
  };
  mutable uint32_t loans = 0;
  bool combined = false;
  uint32_t frameBufferLoanCount() const { return loans; }
  Caps grayscaleCapabilities() const {
    return {combined ? HalDisplay::GrayscaleBase::Combined : HalDisplay::GrayscaleBase::Separate};
  }
  void displayBuffer(HalDisplay::Mode mode) const { std::printf("display %d\n", mode); }
  void displayBufferAsync(HalDisplay::Mode mode) const { std::printf("async %d\n", mode); }
  void displayGrayscaleBase(HalDisplay::Mode mode) const { std::printf("base %d\n", mode); }
};
struct CrossPointSettings {
  enum Gesture { TAP_AND_SWIPE = 0, TAP_ONLY = 1, SWIPE_ONLY = 2, INVERTED_TAP = 3 };
  enum Behavior { OFF, CHAPTER_SKIP, ORIENTATION_CHANGE };
  static constexpr int ORIENTATION_COUNT = 4;
  int longPressButtonBehavior = OFF;
  uint8_t orientation = 0;
  int frequency = 3;
  int getRefreshFrequency() const { return frequency; }
} SETTINGS;
struct MappedInputManager {
  enum class Button { Power, Down };
  unsigned long held = 0;
  bool power = false;
  bool down = false;
  bool wasReleased(Button button) const { return button == Button::Power ? power : down; }
  unsigned long getHeldTime() const { return held; }
};
namespace ReaderUtils {
constexpr unsigned long SKIP_HOLD_MS = 700;
struct PageTurnResult {
  bool prev;
  bool next;
  bool fromTilt;
};
struct TouchPageTurn {
  bool prev;
  bool next;
  unsigned long heldMs;
};
inline PageTurnResult pageInput{};
inline PageTurnResult detectPageTurn(const MappedInputManager&) { return pageInput; }
}  // namespace ReaderUtils
namespace haptic_feedback {
void touchAction(bool strong = false) { std::printf("haptic %d\n", strong); }
}  // namespace haptic_feedback
static bool rawContact = false;
bool buttonContactClosed() { return rawContact; }
struct Section {
  int currentPage = 0;
  uint16_t pageCount = 3;
  bool building = false;
  bool complete = false;
  bool buildOk = true;
  bool loan = false;
  GfxRenderer* renderer = nullptr;
  bool isBuilding() const { return building; }
  bool isBuildComplete() const { return complete; }
  bool buildSomeMore(int count, bool (*stop)()) {
    std::printf("build %d %d\n", count, stop());
    if (loan) ++renderer->loans;
    return buildOk;
  }
};

struct EpubReaderActivity {
  enum class Overlay { None, Toolbar };
  std::unique_ptr<Section> section = std::make_unique<Section>();
  std::shared_ptr<Epub> epub = std::make_shared<Epub>();
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  std::optional<uint16_t> pendingPageJump;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  unsigned long lastPageTurnTime = 0;
  int8_t pendingManualTurn = 0;
  bool pendingManualTurnTouch = false;
  MappedInputManager mappedInput;
  ReaderUtils::TouchPageTurn touchInput{};
  bool endInput = false;
  Overlay overlay = Overlay::None;
  GfxRenderer renderer;
  std::atomic<bool> pageBufferStale{false};
  bool buildHeapPaused = false;
  bool buttonDown = false;
  bool reposition = false;
  int backgroundBuildFailedSpine = -1;
  static constexpr size_t BACKGROUND_BUILD_MIN_FREE_HEAP = 32768;
  static constexpr size_t BACKGROUND_BUILD_MIN_MAX_ALLOC = 16384;
  static constexpr int BACKGROUND_BUILD_PAGES_PER_TICK = 2;
  struct SavedPosition {
    int spineIndex;
    int pageNumber;
  };
  static constexpr int MAX_FOOTNOTE_DEPTH = 3;
  SavedPosition savedPositions[MAX_FOOTNOTE_DEPTH] = {};
  int footnoteDepth = 0;
  EpubReaderActivity() { section->renderer = &renderer; }
  void clearDeferredReposition() {
    std::puts("clear-reposition");
    pendingPageJump.reset();
  }
  bool applyDeferredReposition() {
    std::printf("reposition %d\n", reposition);
    return reposition;
  }
  bool anyButtonPressed() const { return buttonDown; }
  void requestUpdate() { std::puts("update"); }
  bool pageTurn(bool);
#if __has_include(<xp_ui_reader.h>)
  bool applyPageMove(const XpUiReaderMove&, bool);
#endif
  bool skipPages(int);
  bool buildTickHeapGate();
  void backgroundBuildTick();
  bool backgroundBuildWanted() const;
  bool skipLoopDelay();
  void saveLinkStack() const;
  void loadLinkStack();
  void restoreSavedPosition();
  void loadProgress();
  void turnInput();
  bool handleEndOfBookPageTurn(bool, bool) { return endInput; }
  void notePageTurn(bool forward, bool ok) { std::printf("note-turn %d %d\n", forward, ok); }
  void applyOrientation(uint8_t orientation) {
    SETTINGS.orientation = orientation;
    std::printf("orientation %u\n", orientation);
  }
};

#include "reader_methods.inc"

static void sessions() {
  for (uint32_t start : {0u, 1000u, UINT32_MAX - 2500}) {
    for (uint32_t elapsed : {0u, 999u, 1999u, 2000u, 9999u, 10000u, 1800000u, 1800999u}) {
      for (int epoch : {-1, 0, 12345}) {
        for (int progress : {-1, 100, 10001}) {
          ReaderSession session;
          session.onRenderComplete(start, epoch, 500);
          for (int mode = 0; mode < 4; ++mode) {
            session.noteTurn(mode & 1, mode & 2);
            session.onRenderComplete(start + elapsed * (mode + 1), epoch + 100, progress);
            std::printf("session %u %u %d %d %d %lld %lld %u %u %u %d\n", start, elapsed, epoch, progress, mode,
                        static_cast<long long>(session.startTime()), static_cast<long long>(session.endTime()),
                        session.durationSeconds(), session.startProgressBp(), session.endProgressBp(),
                        session.isEmitWorthy());
          }
          session.reset();
          std::printf("reset %lld %lld %u %u %u %d\n", static_cast<long long>(session.startTime()),
                      static_cast<long long>(session.endTime()), session.durationSeconds(), session.startProgressBp(),
                      session.endProgressBp(), session.isEmitWorthy());
        }
      }
    }
  }
}

static void turns() {
  for (int spine = 0; spine <= 3; ++spine) {
    for (int page = 0; page <= 2; ++page) {
      for (int count : {0, 1, 3}) {
        for (int building = 0; building < 2; ++building) {
          for (int action = 0; action < 4; ++action) {
            EpubReaderActivity a;
            a.currentSpineIndex = spine;
            a.section->currentPage = page;
            a.section->pageCount = count;
            a.section->building = building;
            a.pendingPageJump = 12;
            std::printf("turn-input %d %d %d %d %d\n", spine, page, count, building, action);
            bool ok = action < 2 ? a.pageTurn(action != 0) : a.skipPages(action == 2 ? -1 : 1);
            std::printf("turn-output %d %d %d %d %d %u %lu\n", ok, a.currentSpineIndex, a.nextPageNumber,
                        a.section ? a.section->currentPage : -1, a.pendingPageJump.has_value(),
                        a.pendingPageJump.value_or(0), a.lastPageTurnTime);
          }
        }
      }
    }
  }
  EpubReaderActivity missing;
  missing.section.reset();
  std::printf("no-section %d %d\n", missing.pageTurn(true), missing.skipPages(1));
}

static void gates() {
  for (int flags = 0; flags < 8; ++flags) {
    bleturner::Status status{};
    status.running = flags & 1;
    status.stopping = flags & 2;
    status.starting = flags & 4;
    std::printf("gates %d %d %d\n", flags, chapterBuildMayRun(status),
                remoteTurnAccepted(flags & 1, flags & 2, flags & 4));
  }
  for (uint32_t start : {0u, UINT32_MAX - 1000}) {
    for (uint32_t readyAt : {0u, 1u, 4999u, 5000u, 5001u, UINT32_MAX}) {
      uint32_t now = start;
      int calls = 0;
      bool ok = waitForBuildRoom(
          [&] {
            ++calls;
            if (calls == 1) now += 3000;
            return now - (start + 3000) >= readyAt;
          },
          [&] { return now; }, [&](uint32_t ms) { now += ms; }, 5000, 50);
      std::printf("wait %u %u %d %u %d\n", start, readyAt, ok, now, calls);
    }
  }
  for (int flags = 0; flags < 1024; ++flags) {
    EpubReaderActivity a;
    RenderLock::available = !(flags & 1);
    a.overlay = flags & 2 ? EpubReaderActivity::Overlay::Toolbar : EpubReaderActivity::Overlay::None;
    radioHeap = flags & 4;
    rawContact = flags & 8;
    a.buttonDown = flags & 16;
    a.pendingManualTurn = flags & 32;
    ESP.free = flags & 64 ? 32767 : 32768;
    ESP.block = flags & 128 ? 16383 : 16384;
    a.section->building = true;
    a.section->buildOk = !(flags & 256);
    a.section->loan = flags & 512;
    a.section->complete = true;
    a.reposition = true;
    std::printf("build-input %d\n", flags);
    a.backgroundBuildTick();
    std::printf("build-output %d %d %d %d %d\n", a.section != nullptr, a.pageBufferStale.load(), a.buildHeapPaused,
                a.backgroundBuildFailedSpine, a.skipLoopDelay());
  }
  RenderLock::available = true;
  radioHeap = rawContact = false;
}

static void persistence() {
  for (int value : {-1, 0, 1, 65534, 65535, 65536}) {
    for (int column = 0; column < 3; ++column) {
      for (int offset = 0; offset < 2; ++offset) {
        storageTrace = {};
        const bool ok = EpubReaderUtils::saveProgress(Epub{}, column == 0 ? value : 1, column == 1 ? value : 2,
                                                      column == 2 ? value : 3,
                                                      offset ? std::optional<uint32_t>(0x89abcdef) : std::nullopt);
        std::printf("save %d %d %d %d\n", value, column, offset, ok);
        EpubReaderActivity a;
        a.loadProgress();
        std::printf("load %d %d %d %d %d %u\n", a.currentSpineIndex, a.nextPageNumber, a.cachedSpineIndex,
                    a.cachedChapterTotalPageCount, a.cachedVisibleTextOffset.has_value(),
                    a.cachedVisibleTextOffset.value_or(0));
      }
    }
  }
  for (int failure = 0; failure < 4; ++failure) {
    storageTrace = {};
    storageTrace.openOk = failure != 1;
    storageTrace.writeLimit = failure == 2 ? 3 : 100;
    storageTrace.renameOk = failure != 3;
    storageTrace.files["/cache/progress.bin"] = {4, 3, 2, 1};
    const bool ok = EpubReaderUtils::saveProgress(Epub{}, 2, 3, 4);
    std::printf("save-failure %d %d %d %d\n", failure, ok, Storage.exists("/cache/progress.bin"),
                Storage.exists("/cache/progress.bin.tmp"));
  }
  for (int size = 0; size <= 14; ++size) {
    storageTrace = {};
    storageTrace.files["/cache/progress.bin"] = std::vector<uint8_t>(size, 0xff);
    EpubReaderActivity a;
    a.loadProgress();
    std::printf("load-size %d %d %d %d %d %u\n", size, a.currentSpineIndex, a.nextPageNumber,
                a.cachedChapterTotalPageCount, a.cachedVisibleTextOffset.has_value(),
                a.cachedVisibleTextOffset.value_or(0));
  }
  for (int depth = 0; depth <= 3; ++depth) {
    storageTrace = {};
    EpubReaderActivity a;
    a.footnoteDepth = depth;
    for (int i = 0; i < 3; ++i) a.savedPositions[i] = {i, i * 1000};
    a.saveLinkStack();
    EpubReaderActivity b;
    b.loadLinkStack();
    std::printf("links %d %d %d\n", depth, b.footnoteDepth, Storage.exists("/cache/links.bin"));
    for (const auto& p : b.savedPositions) std::printf("position %d %d\n", p.spineIndex, p.pageNumber);
  }
}

static void corruptPersistence() {
  for (int size = 0; size <= 14; ++size) {
    for (int depth = 0; depth <= 4; ++depth) {
      for (int invalidSpine = 0; invalidSpine < 2; ++invalidSpine) {
        storageTrace = {};
        auto& bytes = storageTrace.files["/cache/links.bin"];
        bytes.assign(size, 0);
        if (size) bytes[0] = depth;
        if (size > 5) bytes[5] = invalidSpine ? 3 : 1;
        EpubReaderActivity a;
        for (auto& p : a.savedPositions) p = {17, 19};
        a.loadLinkStack();
        std::printf("links-invalid %d %d %d %d %d\n", size, depth, invalidSpine, a.footnoteDepth,
                    Storage.exists("/cache/links.bin"));
        for (const auto& p : a.savedPositions) std::printf("position %d %d\n", p.spineIndex, p.pageNumber);
        a.restoreSavedPosition();
        std::printf("links-back %d %d %d\n", a.footnoteDepth, a.currentSpineIndex, a.nextPageNumber);
      }
    }
  }
  for (int len : {0, 4, 6, 10}) {
    storageTrace = {};
    const uint8_t bytes[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::printf("atomic-length %d\n", len);
    std::printf("atomic-result %d\n", ProgressFile::writeAtomic("/cache", bytes, len));
  }
}

static void inputTiming() {
  for (int behavior = 0; behavior < 3; ++behavior) {
    for (int touch = 0; touch < 2; ++touch) {
      for (int initial = 0; initial < 2; ++initial) {
        EpubReaderActivity a;
        SETTINGS.longPressButtonBehavior = behavior;
        SETTINGS.orientation = 0;
        a.section->building = true;
        a.section->currentPage = initial;
        a.lastPageTurnTime = 1000;
        // A tap while rendering, replacement by an opposite tap, then delivery
        // at the 200 ms gap. Captured input is supplied at the same call boundary.
        for (int tick = 0; tick < 8; ++tick) {
          clockMs = 1050 + tick * 50;
          RenderLock::available = tick >= 2;
          ReaderUtils::pageInput = {tick == 1, tick == 0 || tick == 4 || tick == 7, false};
          a.touchInput = {};
          if (touch) {
            a.touchInput = {ReaderUtils::pageInput.prev, ReaderUtils::pageInput.next, tick == 7 ? 700UL : 0UL};
            ReaderUtils::pageInput = {};
          }
          a.mappedInput.held = tick == 7 ? 700 : 0;
          std::printf("input %d %d %d %d %u\n", behavior, touch, initial, tick, clockMs);
          a.turnInput();
          std::printf("input-state %d %d %d %d %d %lu\n", a.currentSpineIndex, a.section ? a.section->currentPage : -1,
                      a.pendingManualTurn, a.pendingManualTurnTouch, SETTINGS.orientation, a.lastPageTurnTime);
        }
      }
    }
  }
  RenderLock::available = true;
  ReaderUtils::pageInput = {};
}

static void chrome() {
  for (int frequency : {1, 2, 3, 10}) {
    SETTINGS.frequency = frequency;
    for (int mode = 0; mode < 4; ++mode) {
      GfxRenderer renderer;
      renderer.combined = mode == 3;
      int count = 0;
      for (int i = 0; i < 15; ++i) {
        if (mode < 2)
          ReaderUtils::displayWithRefreshCycle(renderer, count, mode == 1);
        else
          ReaderUtils::displayBaseWithRefreshCycle(renderer, count);
        std::printf("refresh %d %d %d %d\n", frequency, mode, i, count);
      }
    }
  }
  for (const char* tag : {"", "h", "he", "HE", "ar-SA", "fa_IR", "iw", "iwx", "en", "hebrew"})
    std::printf("rtl %s %d\n", tag, ReaderUtils::isRtlBookLanguage(tag));
  for (int gesture = 0; gesture < 6; ++gesture)
    std::printf("gesture %d %d %d\n", gesture, ReaderUtils::gestureAllowsTap(gesture),
                ReaderUtils::gestureAllowsSwipe(gesture));
  for (int title = 0; title < 2; ++title) {
    for (int oldNote = 0; oldNote < 5; ++oldNote) {
      for (int newNote = 0; newNote < 5; ++newNote) {
        LinkNoteTitle note;
        int drawn = static_cast<int>(note.draw(title, static_cast<bleturner::LinkNote>(oldNote)));
        bool first = note.repaint(static_cast<bleturner::LinkNote>(newNote));
        bool second = note.repaint(static_cast<bleturner::LinkNote>(newNote));
        std::printf("note %d %d %d %d %d %d\n", title, oldNote, newNote, drawn, first, second);
      }
    }
  }
}

static void turnLockTiming() {
  traceClock = true;
  for (int spine : {0, 1, 2}) {
    for (int page : {0, 2}) {
      for (bool forward : {false, true}) {
        EpubReaderActivity a;
        a.currentSpineIndex = spine;
        a.section->currentPage = page;
        std::printf("turn-lock %d %d %d\n", spine, page, forward);
        a.pageTurn(forward);
      }
    }
  }
  traceClock = false;
}

static void progressFileLifetime() {
  for (int failure = 0; failure < 4; ++failure) {
    storageTrace = {};
    storageTrace.trackLifetime = true;
    storageTrace.openOk = failure != 1;
    storageTrace.writeLimit = failure == 2 ? 2 : 100;
    storageTrace.renameOk = failure != 3;
    const uint8_t bytes[] = {1, 2, 3, 4, 5, 6};
    std::printf("progress-lifetime %d\n", failure);
    const bool result = ProgressFile::writeAtomic("/cache", bytes, sizeof(bytes));
    std::printf("progress-done %d %d\n", result, storageTrace.liveFiles);
  }
  storageTrace.trackLifetime = false;
}

int main() {
  sessions();
  turns();
  gates();
  inputTiming();
  persistence();
  chrome();
  corruptPersistence();
  turnLockTiming();
  progressFileLifetime();
}
